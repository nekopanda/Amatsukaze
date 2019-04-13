using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

namespace Amatsukaze.Server
{
    class PauseScheduler
    {
        private EncodeServer server;
        private WorkerPool workerPool;

        private Task timerThread;
        private BufferBlock<int> timerQ = new BufferBlock<int>();

        public PauseScheduler(EncodeServer server, WorkerPool workerPool)
        {
            this.server = server;
            this.workerPool = workerPool;
            NotifySettingChanged();
        }

        private async Task TimerFunc()
        {
            Task<bool> timerQRecvTask = timerQ.OutputAvailableAsync();

            while (true)
            {
                var setting = server.AppData_.setting;
                var now = DateTime.Now;
                var isPauseHour = setting.EnableRunHours && !setting.RunHours[now.Hour];

                workerPool.SetPause(isPauseHour, true);
                var suspend = isPauseHour && setting.RunHoursSuspendEncoders;
                // workersの数が変わってるかもしれないので毎回行う
                foreach(var worker in workerPool.Workers.OfType<TranscodeWorker>())
                {
                    worker.SetSuspend(suspend, true);
                }

                if (setting.EnableRunHours == false)
                {
                    break;
                }

                await server.RequestState();

                var future = now.AddMinutes(60);
                var elapsed = new DateTime(future.Year, future.Month, future.Day, future.Hour, 0, 0) - now;

                if(await Task.WhenAny(timerQRecvTask, Task.Delay(elapsed)) == timerQRecvTask)
                {
                    if(timerQRecvTask.Result == false)
                    {
                        // 完了した
                        break;
                    }
                    timerQ.Receive();
                    timerQRecvTask = timerQ.OutputAvailableAsync();
                }
            }

            timerThread = null;
            await server.RequestState();
        }

        public void NotifySettingChanged()
        {
            if(timerThread == null && server.AppData_.setting.EnableRunHours)
            {
                timerThread = TimerFunc();
            }
            else if(timerThread != null)
            {
                timerQ.Post(0);
            }
        }

        public void Complete()
        {
            timerQ.Complete();
        }
    }
}
