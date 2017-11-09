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
        Task<bool> RunItem(ITEM item);
    }

    class EncodeScheduler<ITEM>
    {
        private class Worker
        {
            public bool IsSleeping;
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

        private Stack<ITEM> stack = new Stack<ITEM>();
        private Queue<ITEM> queue = new Queue<ITEM>();

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

                    int failCount = 0;
                    int runCount = 0;
                    while (id < numActive)
                    {
                        ITEM item;
                        if(stack.Count > 0)
                        {
                            item = stack.Pop();
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
                        if (await w.TargetWorker.RunItem(item))
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

        private void SetActive(int active)
        {
            while (running.Count + parking.Count < active)
            {
                var worker = workers.First(w => w.IsSleeping);
                worker.IsSleeping = false;
                parking.Add(worker);
                worker.NotifyQ.Post(0);
            }
            numActive = active;
        }

        public void SetNumParallel(int parallel)
        {
            while (workers.Count < parallel)
            {
                int id = workers.Count;
                workers.Add(new Worker() {
                    IsSleeping = true,
                    NotifyQ = new BufferBlock<int>(),
                    TargetWorker = NewWorker(id)
                });
                workerThreads.Add(WorkerThread(id));
            }
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

        public void StackItem(ITEM item)
        {
            stack.Push(item);
            foreach(var w in parking)
            {
                w.NotifyQ.Post(0);
            }
        }

        public void QueueItem(ITEM item)
        {
            queue.Enqueue(item);
            foreach (var w in parking)
            {
                w.NotifyQ.Post(0);
            }
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
