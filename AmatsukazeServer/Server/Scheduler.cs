using Priority_Queue;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

namespace Amatsukaze.Server
{
    interface IScheduleWorker<ITEM>
    {
        Task<bool> RunItem(ITEM item, bool forceStart);
    }

    class EncodeScheduler<ITEM> where ITEM : class
    {
        private class Worker
        {
            public bool IsSleeping;
            public ITEM WorkItem; // 強制的に実行するアイテム
            public BufferBlock<int> NotifyQ;
            public IScheduleWorker<ITEM> TargetWorker;
        }

        private List<Worker> workers = new List<Worker>();
        private List<Task> workerThreads = new List<Task>();

        private List<Worker> running = new List<Worker>();
        private List<Worker> parking = new List<Worker>();

        public Func<int, IScheduleWorker<ITEM>> NewWorker;
        public Func<Task> OnStart;
        public Func<Task> OnFinish;
        public Func<string, Task> OnError;

        public IEnumerable<IScheduleWorker<ITEM>> Workers { get { return workers.Select(w => w.TargetWorker); } }

        private SimplePriorityQueue<ITEM> queue = new SimplePriorityQueue<ITEM>();

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
                        ITEM item;
                        if(w.WorkItem != null)
                        {
                            item = w.WorkItem;
                        }
                        else if(queue.Count > 0)
                        {
                            item = queue.Dequeue();
                        }
                        else
                        {
                            running.Remove(w);
                            parking.Add(w);
                            if(runCount > 0 && running.Count == 0)
                            {
                                // 自分が最後
                                await OnFinish();
                            }
                            break;
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

        private void ActivateOneWorker(ITEM item)
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

        public void QueueItem(ITEM item, float priority)
        {
            if (priority > 0) throw new Exception("エラー");
            queue.Enqueue(item, priority + 10);
            foreach (var w in parking)
            {
                w.NotifyQ.Post(0);
            }
        }

        public void UpdatePriority(ITEM item, float priority)
        {
            if (priority > 0) throw new Exception("エラー");
            queue.TryUpdatePriority(item, priority + 10);
        }

        // アイテムを１つだけ強制的に開始する
        public void ForceStart(ITEM item)
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
}
