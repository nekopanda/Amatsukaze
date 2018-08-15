using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    /// <summary>
    /// リソース管理
    /// </summary>
    class ResourceManager
    {
        public static readonly int MAX_GPU = 16;
        public static readonly int MAX = 100;

        private class WaitinResource
        {
            public ReqResource Req;
            public int Cost;
        }

        private TaskCompletionSource<int> waitTask = new TaskCompletionSource<int>();
        private int curHDD, curCPU;
        private List<WaitinResource> waitingResources = new List<WaitinResource>();

        private int numGPU;
        private int[] curGPU;
        private int[] maxGPU;

        // エンコーダの番号
        private List<int> encodeIds;

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
            RecalculateCosts();

            // 待っている人全員に通知
            SignalAll();
        }

        private void RecalculateCosts()
        {
            foreach (var w in waitingResources)
            {
                w.Cost = ResourceCost(w.Req);
            }
            waitingResources.Sort((a, b) => a.Cost - b.Cost);
        }

        private void DoRelResource(Resource res)
        {
            curCPU -= res.Req.CPU;
            curHDD -= res.Req.HDD;
            curGPU[res.GpuIndex] -= res.Req.GPU;
            if(res.EncoderIndex != -1)
            {
                encodeIds.Remove(res.EncoderIndex);
            }
            RecalculateCosts();
        }

        // 最も余裕のあるGPUを返す
        private int MostCapableGPU()
        {
            var GPUSpace = Enumerable.Range(0, numGPU).Select(i => maxGPU[i] - curGPU[i]).ToList();
            return GPUSpace.IndexOf(GPUSpace.Max());
        }

        private int AllocateEncoderIndex()
        {
            for(int i = 0; ; ++i)
            {
                if(!encodeIds.Contains(i))
                {
                    encodeIds.Add(i);
                    return i;
                }
            }
        }

        public int ResourceCost(ReqResource req)
        {
            int gpuIndex = MostCapableGPU();
            int nextCPU = curCPU + req.CPU;
            int nextHDD = curHDD + req.HDD;
            int nextGPU = curGPU[gpuIndex] + req.GPU;
            return Math.Max(Math.Max(nextCPU - MAX, nextHDD - MAX), nextGPU - maxGPU[gpuIndex]);
        }

        // 上限を無視してリソースを確保
        public Resource ForceGetResource(ReqResource req, bool reqEncoderIndex)
        {
            int gpuIndex = MostCapableGPU();
            curCPU += req.CPU;
            curHDD += req.HDD;
            curGPU[gpuIndex] += req.GPU;
            RecalculateCosts();

            return new Resource()
            {
                Req = req,
                GpuIndex = gpuIndex,
                EncoderIndex = reqEncoderIndex ? AllocateEncoderIndex() : -1
            };
        }

        public Resource TryGetResource(ReqResource req, bool reqEncoderIndex)
        {
            int cost = ResourceCost(req);

            if(cost > 0)
            {
                // 上限を超えるのでダメ
                return null;
            }

            if (waitingResources.Count > 0)
            {
                // 待っている人がいる場合は、コストが最小値以下でない場合はダメ
                if (cost > waitingResources[0].Cost)
                {
                    return null;
                }
            }

            // OK
            return ForceGetResource(req, reqEncoderIndex);
        }

        /// <summary>
        /// リソースを確保する
        /// </summary>
        /// <param name="req">次のフェーズで必要なリソース</param>
        /// <param name="cancelToken">キャンセルトークン</param>
        /// <returns>確保されたリソース</returns>
        public async Task<Resource> GetResource(ReqResource req, CancellationToken cancelToken, bool reqEncoderIndex)
        {
            var waiting = new WaitinResource() { Req = req };

            cancelToken.Register((Action)(() =>
            {
                waitingResources.Remove(waiting);
                // キャンセルされたら一旦動かす
                SignalAll();
            }), true);

            waitingResources.Add(waiting);
            RecalculateCosts();

            while (true)
            {
                // リソース確保可能 かつ 最小コスト
                if(waiting.Cost <= 0 && waiting.Cost <= waitingResources[0].Cost)
                {
                    waitingResources.Remove(waiting);
                    var res = ForceGetResource(req, reqEncoderIndex);
                    SignalAll();
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
