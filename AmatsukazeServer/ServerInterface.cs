using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EncodeServer
{
    public interface IEncodeServer
    {
        // 操作系
        Task SetSetting(Setting setting);
        Task AddQueue(string dirPath);
        Task RemoveQueue(string dirPath);
        Task PauseEncode(bool pause);

        // 情報取得系
        Task RequestQueue();
        Task RequestLog();
        Task RequestConsole();
        Task RequestLogFile(LogItem item);
        Task RequestState();
    }

    public interface IUserClient
    {
        Task OnQueueData(QueueData data);
        Task OnQueueUpdate(QueueUpdate update);
        Task OnLogData(LogData data);
        Task OnLogUpdate(LogItem newLog);
        Task OnConsole(string str);
        Task OnConsoleUpdate(string str);
        Task OnLogFile(string str);
        Task OnState(State state);
        Task OnOperationResult(string result);
    }

    public enum RPCMethodId
    {
        SetSetting = 100,
        AddQueue,
        RemoveQueue,
        PauseEncode,
        RequestQueue,
        RequestLog,
        RequestConsole,
        RequestLogFile,
        RequestState,

        OnQueueData = 200,
        OnQueueUpdate,
        OnLogData,
        OnLogUpdate,
        OnConsole,
        OnConsoleUpdate,
        OnLogFile,
        OnState,
        OnOperationResult,
    }

    public static class RPCTypes
    {
        public static readonly Dictionary<RPCMethodId, Type> ArgumentTypes = new Dictionary<RPCMethodId, Type>() {
            { RPCMethodId.SetSetting, typeof(Setting) },
            { RPCMethodId.AddQueue, typeof(string) },
            { RPCMethodId.RemoveQueue, typeof(string) },
            { RPCMethodId.PauseEncode, typeof(bool) },
            { RPCMethodId.RequestQueue, null },
            { RPCMethodId.RequestLog, null },
            { RPCMethodId.RequestConsole, null },
            { RPCMethodId.RequestLogFile, typeof(LogItem) },
            { RPCMethodId.RequestState, null },

            { RPCMethodId.OnQueueData, typeof(QueueData) },
            { RPCMethodId.OnQueueUpdate, typeof(QueueUpdate) },
            { RPCMethodId.OnLogData, typeof(LogData) },
            { RPCMethodId.OnLogUpdate, typeof(LogItem) },
            { RPCMethodId.OnConsole, typeof(string) },
            { RPCMethodId.OnConsoleUpdate, typeof(string) },
            { RPCMethodId.OnLogFile, typeof(string) },
            { RPCMethodId.OnState, typeof(State) },
            { RPCMethodId.OnOperationResult, typeof(string) }
        };
    }
}
