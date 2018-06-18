using Priority_Queue;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

namespace Amatsukaze.Server
{
    interface IScheduleWorker
    {
        Task<bool> RunItem(QueueItem item, bool forceStart);
    }

    class WorkerPool
    {
        private class Worker
        {
            public bool IsSleeping;
            public QueueItem WorkItem; // 強制的に実行するアイテム
            public BufferBlock<int> NotifyQ;
            public IScheduleWorker TargetWorker;
        }

        private List<Worker> workers = new List<Worker>();
        private List<Task> workerThreads = new List<Task>();

        private List<Worker> running = new List<Worker>();
        private List<Worker> parking = new List<Worker>();

        public ScheduledQueue Queue;

        public Func<int, IScheduleWorker> NewWorker;
        public Func<Task> OnStart;
        public Func<Task> OnFinish;
        public Func<string, Task> OnError;

        public IEnumerable<IScheduleWorker> Workers { get { return workers.Select(w => w.TargetWorker); } }

        private int numParallel;
        private bool isPause;

        private int numActive;

        private async Task WorkerThread(int id)
        {
            try
            {
                var w = workers[id];
                while (await w.NotifyQ.OutputAvailableAsync())
                {
                    await workers[id].NotifyQ.ReceiveAsync();

                    if(parking.Remove(w) == false)
                    {
                        // 自分は呼ばれてなかった
                        continue;
                    }

                    bool isFirst = (running.Count == 0);

                    running.Add(w);

                    int failCount = 0; // 今は使ってない...
                    int runCount = 0;
                    while (id < numActive || w.WorkItem != null)
                    {
                        QueueItem item;
                        if(w.WorkItem != null)
                        {
                            item = w.WorkItem;
                            Queue.StartItem(item);
                        }
                        else
                        {
                            item = Queue.PopItem();
                            if(item == null)
                            {
                                running.Remove(w);
                                parking.Add(w);
                                if (runCount > 0 && running.Count == 0)
                                {
                                    // 自分が最後
                                    await OnFinish();
                                }
                                break;
                            }
                        }
                        if (runCount == 0 && isFirst)
                        {
                            // 自分が最初に開始した
                            await OnStart();
                        }
                        ++runCount;
                        if (await w.TargetWorker.RunItem(item, w.WorkItem != null))
                        {
                            failCount = 0;
                        }
                        Queue.ReleaseItem(item);
                        if (failCount > 0)
                        {
                            int waitSec = (failCount * 10 + 10);
                            await Task.WhenAll(
                                OnError("エンコードに失敗したので" + waitSec + "秒待機します。(parallel=" + id + ")"),
                                Task.Delay(waitSec * 1000));
                        }
                        w.WorkItem = null;
                    }

                    if(id >= numActive)
                    {
                        // sleep要求
                        running.Remove(w);
                        parking.Remove(w);
                        w.IsSleeping = true;
                    }
                }
            }
            catch (Exception exception)
            {
                await OnError("EncodeThreadがエラー終了しました: " + exception.Message);
            }
        }

        /// <summary>
        /// キューにアイテムが追加されたことを通知
        /// </summary>
        public void NotifyAddQueue()
        {
            foreach (var w in parking)
            {
                w.NotifyQ.Post(0);
            }
        }

        private void ActivateOneWorker(QueueItem item)
        {
            var worker = workers.First(w => w.IsSleeping);
            worker.IsSleeping = false;
            worker.WorkItem = item;
            parking.Add(worker);
            worker.NotifyQ.Post(0);
        }

        private void SetActive(int active)
        {
            while (running.Count + parking.Count < active)
            {
                ActivateOneWorker(null);
            }
            numActive = active;
        }

        private void EnsureNumWorkers(int numWorkers)
        {
            while (workers.Count < numWorkers)
            {
                int id = workers.Count;
                workers.Add(new Worker()
                {
                    IsSleeping = true,
                    NotifyQ = new BufferBlock<int>(),
                    TargetWorker = NewWorker(id)
                });
                workerThreads.Add(WorkerThread(id));
            }
        }

        public void SetNumParallel(int parallel)
        {
            EnsureNumWorkers(parallel);
            numParallel = parallel;
            if(isPause == false)
            {
                SetActive(numParallel);
            }
        }

        public void SetPause(bool pause)
        {
            if(isPause != pause)
            {
                isPause = pause;
                SetActive(isPause ? 0 : numParallel);
            }
        }

        // アイテムを１つだけ強制的に開始する
        public void ForceStart(QueueItem item)
        {
            EnsureNumWorkers(running.Count + parking.Count + 1);
            ActivateOneWorker(item);
        }

        // タスクを待たずに終了させる
        public Task Finish()
        {
            SetActive(0);
            foreach(var w in workers)
            {
                w.NotifyQ.Complete();
            }
            return Task.WhenAll(workerThreads);
        }
    }

    /// <summary>
    /// AddQueue->(必要に応じてMakeDirty)->PopItem->ReleaseItemの順に呼んで使う
    /// </summary>
    class ScheduledQueue
    {
        private static readonly int EncodePhase = (int)ResourcePhase.Encode;

        private struct ItemPair
        {
            public QueueItem Item;
            public Resource Resource;
        }

        public WorkerPool WorkerPool { get; set; }

        private Dictionary<int, List<QueueItem>>[] queue =
            Enumerable.Range(0, 5).Select(s => new Dictionary<int, List<QueueItem>>()).ToArray();
        private bool isDirty;

        private List<ItemPair> actives = new List<ItemPair>();

        private ResourceManager resourceManager = new ResourceManager();

        public bool EnableResourceScheduling { get; set; }

        public void AddQueue(QueueItem item)
        {
            if(item.Priority < 1)
            {
                item.Priority = 1;
            }
            if(item.Priority > 5)
            {
                item.Priority = 5;
            }
            int key = item.Profile.ReqResources[EncodePhase].Canonical();
            var level = queue[item.Priority - 1];
            if(level.ContainsKey(key) == false)
            {
                level[key] = new List<QueueItem>();
            }
            level[key].Add(item);
            WorkerPool.NotifyAddQueue();
        }

        public bool RemoveQueue(QueueItem item)
        {
            foreach(var level in queue)
            {
                foreach(var entry in level)
                {
                    if(entry.Value.Remove(item))
                    {
                        if(entry.Value.Count == 0)
                        {
                            level.Remove(entry.Key);
                        }
                        return true;
                    }
                }
            }
            return false;
        }

        // アイテムの優先度やリソースを変更した場合は呼ぶ
        public void MakeDirty()
        {
            isDirty = true;
        }

        private static bool IsInConsistent(QueueItem item, int priority, int key)
        {
            return item.Priority != priority ||
                   key != item.Profile.ReqResources[EncodePhase].Canonical();
        }

        public void Clean()
        {
            List<QueueItem> tmp = new List<QueueItem>();
            // Queue状態以外のアイテムの削除
            for(int i = 0; i < queue.Length; ++i)
            {
                var level = queue[i];
                var priority = i + 1;
                foreach (var key in level.Keys)
                {
                    // 削除アイテムがある？
                    if(level[key].Any(
                        s => s.State != QueueState.Queue || IsInConsistent(s, priority, key)))
                    {
                        // 必要ないものは消す
                        var filtered = level[key].Where(s => s.State == QueueState.Queue);
                        // Priority,Resourceが適切でないアイテム
                        var ngItems = filtered.Where(s => IsInConsistent(s, priority, key));
                        // 適切なアイテム
                        var okItems = filtered.Where(s => !IsInConsistent(s, priority, key));
                        // Priority,Resourceが適切でないアイテムは一時リストに移動
                        tmp.AddRange(ngItems);
                        // 適切なアイテムは戻す
                        level[key] = filtered.Where(s => !IsInConsistent(s, priority, key)).ToList();
                    }
                }
            }
            // 一時リストを戻す
            foreach(var item in tmp)
            {
                AddQueue(item);
            }
            // フラグを消す
            isDirty = false;
        }

        private QueueItem NextItem()
        {
            for (int i = queue.Length - 1; i >= 0; --i)
            {
                var level = queue[i];
                if (EnableResourceScheduling)
                {
                    if (level.Any())
                    {
                        var resList = queue[i].Select(e => e.Key).ToList();
                        var costList = resList.Select(s => resourceManager.ResourceCost(ReqResource.FromCanonical(s))).ToList();
                        var minKey = resList[costList.IndexOf(costList.Min())];
                        if (level[minKey].Any())
                        {
                            return level[minKey].First();
                        }
                    }
                }
                else
                {
                    var item = level.FirstOrDefault().Value?.FirstOrDefault();
                    if (item != null)
                    {
                        return item;
                    }
                }
            }

            return null;
        }

        public QueueItem PopItem()
        {
            if(isDirty)
            {
                Clean();
            }
            var item = NextItem();
            if(item != null)
            {
                RemoveQueue(item);
                actives.Add(new ItemPair()
                {
                    Item = item,
                    Resource = resourceManager.ForceGetResource(item.Profile.ReqResources[EncodePhase])
                });
                return item;
            }
            return null;
        }

        public void StartItem(QueueItem item)
        {
            resourceManager.ForceGetResource(item.Profile.ReqResources[EncodePhase]);
        }

        public void ReleaseItem(QueueItem item)
        {
            int index = actives.FindIndex(s => s.Item == item);
            if (index == -1)
            {
                throw new ArgumentException("指定されたアイテムは実行中ではありません");
            }
            else
            {
                resourceManager.ReleaseResource(actives[index].Resource);
                actives.RemoveAt(index);
            }
        }

        public void SetGPUResources(int numGPU, int[] maxGPU)
        {
            resourceManager.SetGPUResources(numGPU, maxGPU);
        }
    }
}

