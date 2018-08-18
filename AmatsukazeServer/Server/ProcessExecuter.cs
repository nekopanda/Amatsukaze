using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    public interface IProcessExecuter : IDisposable
    {
        void Canel();
    }

    public class NormalProcess : IProcessExecuter
    {
        public Process Process { get; private set; }

        public Func<byte[], int, int, Task> OnOutput;

        public NormalProcess(ProcessStartInfo psi)
        {
            Process = System.Diagnostics.Process.Start(psi);
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                if (disposing)
                {
                    // TODO: マネージ状態を破棄します (マネージ オブジェクト)。
                }

                // アンマネージ リソース (アンマネージ オブジェクト) を解放し、下のファイナライザーをオーバーライドします。
                Process.Dispose();
                // 大きなフィールドを null に設定します。
                Process = null;

                disposedValue = true;
            }
        }

        // TODO: 上の Dispose(bool disposing) にアンマネージ リソースを解放するコードが含まれる場合にのみ、ファイナライザーをオーバーライドします。
        // ~NormalProcess() {
        //   // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
        //   Dispose(false);
        // }

        // このコードは、破棄可能なパターンを正しく実装できるように追加されました。
        public void Dispose()
        {
            // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
            Dispose(true);
            // TODO: 上のファイナライザーがオーバーライドされる場合は、次の行のコメントを解除してください。
            // GC.SuppressFinalize(this);
        }
        #endregion

        private async Task RedirectOut(Stream stream)
        {
            try
            {
                byte[] buffer = new byte[1024];
                while (true)
                {
                    var readBytes = await stream.ReadAsync(buffer, 0, buffer.Length);
                    if (readBytes == 0)
                    {
                        // 終了
                        return;
                    }
                    if (OnOutput != null)
                    {
                        await OnOutput(buffer, 0, readBytes);
                    }
                }
            }
            catch (Exception e)
            {
                Debug.Print("RedirectOut exception " + e.Message);
            }
        }

        public Task WaitForExitAsync()
        {
            return Task.WhenAll(
                RedirectOut(Process.StandardOutput.BaseStream),
                RedirectOut(Process.StandardError.BaseStream),
                Task.Run(() => Process.WaitForExit()));
        }

        public void Canel()
        {
            if(Process != null && Process.HasExited == false)
            {
                string taskkill = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "taskkill.exe");
                using (var procKiller = new System.Diagnostics.Process())
                {
                    procKiller.StartInfo.FileName = taskkill;
                    procKiller.StartInfo.Arguments = string.Format("/PID {0} /T /F", Process.Id);
                    procKiller.StartInfo.CreateNoWindow = true;
                    procKiller.StartInfo.UseShellExecute = false;
                    procKiller.Start();
                    procKiller.WaitForExit();
                }
            }
        }
    }
}
