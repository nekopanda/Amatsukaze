using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

namespace Amatsukaze.Server
{
    class Resource
    {
        public ReqResource Req;
        public int GpuIndex;
    }

    /// <summary>
    /// リソース管理
    /// </summary>
    class ResourceManager
    {
        public static readonly int MAX_GPU = 16;

        private TaskCompletionSource<int> waitTask = new TaskCompletionSource<int>();
        private int curHDD, curCPU;

        private int numGPU;
        private int[] curGPU;
        private int[] maxGPU;

        public ResourceManager()
        {
            numGPU = MAX_GPU;
            curGPU = Enumerable.Repeat(0, MAX_GPU).ToArray();
            maxGPU = Enumerable.Repeat(100, MAX_GPU).ToArray();
        }

        public void SetGPUResources(int numGPU, int[] maxGPU)
        {
            if(numGPU > MAX_GPU)
            {
                throw new ArgumentException("GPU数が最大数を超えています");
            }
            if(numGPU > maxGPU.Length)
            {
                throw new ArgumentException("numGPU > maxGPU.Count");
            }
            this.numGPU = numGPU;
            this.maxGPU = maxGPU;
        }

        private void DoRelResource(Resource res)
        {
            curCPU -= res.Req.CPU;
            curHDD -= res.Req.HDD;
            curGPU[res.GpuIndex] -= res.Req.GPU;
        }

        // 最も余裕のあるGPUを返す
        private int MostCapableGPU()
        {
            var GPUSpace = Enumerable.Range(0, numGPU).Select(i => maxGPU[i] - curGPU[i]).ToList();
            return GPUSpace.IndexOf(GPUSpace.Max());
        }

        // 上限を無視してリソースを確保
        public Resource ForceGetResource(ReqResource req)
        {
            int gpuIndex = MostCapableGPU();

            curCPU += req.CPU;
            curHDD += req.HDD;
            curGPU[gpuIndex] += req.GPU;
            return new Resource()
            {
                Req = req,
                GpuIndex = gpuIndex
            };
        }

        public Resource TryGetResource(ReqResource req)
        {
            const int MAX = 100;

            // CPU,HDDをチェック
            int nextCPU = curCPU + req.CPU;
            int nextHDD = curHDD + req.HDD;
            if (nextCPU > MAX || nextHDD > MAX)
            {
                return null;
            }

            // GPUをチェック
            int gpuIndex = MostCapableGPU();
            int nextGPU = curGPU[gpuIndex] + req.GPU;
            if (nextGPU > maxGPU[gpuIndex])
            {
                return null;
            }

            // OK
            curCPU = nextCPU;
            curHDD = nextHDD;
            curGPU[gpuIndex] = nextGPU;
            return new Resource()
            {
                Req = req,
                GpuIndex = gpuIndex
            };
        }

        /// <summary>
        /// リソースを確保する
        /// </summary>
        /// <param name="req">次のフェーズで必要なリソース</param>
        /// <param name="cancelToken">キャンセルトークン</param>
        /// <returns>確保されたリソース</returns>
        public async Task<Resource> GetResource(ReqResource req, CancellationToken cancelToken)
        {
            cancelToken.Register((Action)(() =>
            {
                // キャンセルされたら一旦動かす
                SignalAll();
            }), true);

            while (true)
            {
                // リソース取得を試みる
                var res = TryGetResource(req);
                if(res != null)
                {
                    //Util.AddLog("リソース確保成功: " + req.CPU + ":" + req.HDD + ":" + req.GPU);
                    return res;
                }
                // リソースに空きがないので待つ
                //Util.AddLog("リソース待ち: " + req.CPU + ":" + req.HDD + ":" + req.GPU);
                await waitTask.Task;
                // キャンセルされてたら例外を投げる
                cancelToken.ThrowIfCancellationRequested();
            }
        }

        private void SignalAll()
        {
            // 現在の待ちを終了させる
            waitTask.SetResult(0);
            // 次の待ち用に新しいタスクを生成しておく
            waitTask = new TaskCompletionSource<int>();
        }

        public void ReleaseResource(Resource res)
        {
            //Util.AddLog("リソース解放: " + res.Req.CPU + ":" + res.Req.HDD + ":" + res.Req.GPU);
            // リソースを解放
            DoRelResource(res);
            // 待っている人全員に通知
            SignalAll();
        }
    }
}
