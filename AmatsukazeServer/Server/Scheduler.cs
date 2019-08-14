using System;
using System.Collections.Generic;
using System.Linq;
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
        private enum State
        {
            InActive, // 無効状態
            Parking,  // 待機状態
            Running   // アイテム実行中
        }
        private class Worker
        {
            public int Id;
            public State State;
            public IScheduleWorker TargetWorker;
        }

        private List<Worker> workers = new List<Worker>();

        private int numRunning;
        private SortedSet<int> parking = new SortedSet<int>(); // 待機状態のワーカーID

        public ScheduledQueue Queue;

        public Func<int, IScheduleWorker> NewWorker;
        public Func<Task> OnStart;
        public Func<Task> OnFinish;
        public Func<int, string, Exception, Task> OnError;

        public IEnumerable<IScheduleWorker> Workers {
            get {
                var lastRunning = workers.LastOrDefault(w => w.State == State.Running);
                var numActive = Math.Max(numParallel,
                    (lastRunning != null) ? (workers.IndexOf(lastRunning) + 1) : 0);
                return workers.Take(numActive).Select(w => w.TargetWorker);
            }
        }

        private int numParallel;

        public bool ScheduledPaused { get; private set; }
        public bool UserPaused { get; private set; }
        public bool IsPaused { get { return ScheduledPaused || UserPaused; } }

        private async void RunItem(Worker worker, QueueItem item, bool forceStart)
        {
            try
            {
                worker.State = State.Running;
                if (numRunning++ == 0)
                {
                    await OnStart();
                }
                try
                {
                    await worker.TargetWorker.RunItem(item, forceStart);
                }
                catch (Exception exception)
                {
                    await OnError(worker.Id, "エンコードが不明なエラーで終了しました", exception);
                }
                finally
                {
                    Queue.ReleaseItem(item);
                }
                worker.State = State.InActive;
                if (IsPaused == false && worker.Id < numParallel)
                {
                    worker.State = State.Parking;
                    parking.Add(worker.Id);
                    ScheduleTask();
                }
                if (--numRunning == 0)
                {
                    await OnFinish();
                }
            }
            catch (Exception exception)
            {
                await OnError(worker.Id, "EncodeThreadがエラー終了しました", exception);
            }
        }

        private void ScheduleTask()
        {
            while(parking.Count > 0)
            {
                var item = Queue.PopItem();
                if(item == null)
                {
                    return;
                }
                var wid = parking.First();
                parking.Remove(wid);
                RunItem(workers[wid], item, false);
            }
        }

        /// <summary>
        /// キューにアイテムが追加されたことを通知
        /// </summary>
        public void NotifyAddQueue()
        {
            ScheduleTask();
        }

        private void EnsureNumWorkers(int numWorkers)
        {
            while (workers.Count < numWorkers)
            {
                int id = workers.Count;
                workers.Add(new Worker()
                {
                    Id = id,
                    State = State.InActive,
                    TargetWorker = NewWorker(id)
                });
                if (IsPaused == false && id < numParallel)
                {
                    workers[id].State = State.Parking;
                    parking.Add(id);
                }
            }
        }

        public void SetNumParallel(int parallel)
        {
            numParallel = parallel;
            EnsureNumWorkers(parallel);
            foreach (var id in parking)
            {
                if(id >= numParallel)
                {
                    workers[id].State = State.InActive;
                }
            }
            parking.RemoveWhere(id => id >= numParallel);
            ScheduleTask();
        }

        public void SetPause(bool pause, bool scheduled)
        {
            var current = IsPaused;
            if(scheduled)
            {
                ScheduledPaused = pause;
            }
            else
            {
                UserPaused = pause;
            }
            if(IsPaused != current)
            {
                if(IsPaused)
                {
                    foreach(var id in parking)
                    {
                        workers[id].State = State.InActive;
                    }
                    parking.Clear();
                }
                else
                {
                    for(int id = 0; id < numParallel; ++id)
                    {
                        if(workers[id].State == State.InActive)
                        {
                            workers[id].State = State.Parking;
                            parking.Add(id);
                        }
                    }
                    ScheduleTask();
                }
            }
        }

        // アイテムを１つだけ強制的に開始する
        public void ForceStart(QueueItem item)
        {
            var idleWorker = workers.FirstOrDefault(w => w.State != State.Running);
            if (idleWorker == null)
            {
                EnsureNumWorkers(workers.Count + 1);
                idleWorker = workers.Last();

                if(idleWorker.State == State.Running)
                {
                    throw new InvalidProgramException();
                }
             }
            if(idleWorker.State == State.Parking)
            {
                parking.Remove(idleWorker.Id);
            }
            Queue.StartItem(item);
            RunItem(idleWorker, item, true);
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
            isDirty = true;
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
                foreach (var key in level.Keys.ToArray())
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
            // ないkeyは消す
            for (int i = 0; i < queue.Length; ++i)
            {
                var level = queue[i];
                foreach(var key in level.Keys.Where(key => !level[key].Any()).ToArray())
                {
                    level.Remove(key);
                }
            }
            // Orderで並べ替え
            foreach(var level in queue)
            {
                foreach(var list in level.Values)
                {
                    list.Sort((a, b) => a.Order - b.Order);
                }
            }
            // フラグを消す
            isDirty = false;
        }

        private static int[][] ResourceSections =
            new int[][] { new int[] { 4 }, new int[] { 3, 2, 1 }, new int[] { 0 } };

        private QueueItem NextItem()
        {
            if(EnableResourceScheduling)
            {
                return ResourceSections
                    // 各リソース区間の優先度リスト
                    .Select(prs => prs
                    // リソース区間のエントリをリスト化
                    // リストは優先度の高い順になっていることに注意
                    .SelectMany(pr => queue[pr])
                    // リソースキーでまとめる
                    .GroupBy(entry => entry.Key)
                    // リソースキーをコストに変換
                    // アイテムはそのリソースキーで最も優先度の高いアイテム１つだけにする
                    // アイテムは優先度順になっているはずなのでこれでOK
                    .Select(g => new {
                        Cost = resourceManager.ResourceCost(ReqResource.FromCanonical(g.Key)),
                        Item = g.First().Value.First()
                    })
                    // リソースの空き具合で並べ替え
                    .OrderBy(g => g.Cost)
                    // 最もリソースの空きの大きいアイテムを選択
                    // アイテムがない場合nullになることに注意
                    .FirstOrDefault()?.Item)
                    // 優先度の高い順になっているので最初のアイテムを返す
                    .FirstOrDefault(s => s != null);
            }
            else
            {
                return queue.Reverse()
                    .Select(level => level.FirstOrDefault().Value?.FirstOrDefault())
                    .FirstOrDefault(s => s != null);
            }
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
                    Resource = resourceManager.ForceGetResource(item.Profile.ReqResources[EncodePhase], false)
                });
                return item;
            }
            return null;
        }

        public void StartItem(QueueItem item)
        {
            actives.Add(new ItemPair()
            {
                Item = item,
                Resource = resourceManager.ForceGetResource(item.Profile.ReqResources[EncodePhase], false)
            });
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

