using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.Serialization;
using System.Threading.Tasks;
using System.Threading;
using System.IO;

using Livet;
using AmatsukazeServer;
using System.Collections.ObjectModel;

namespace AmatsukazeGUI.Models
{
    public class Model : NotificationObject, IUserClient
    {
        /*
         * NotificationObjectはプロパティ変更通知の仕組みを実装したオブジェクトです。
         */
        [DataContract]
        private class ClientData : IExtensibleDataObject
        {
            [DataMember]
            public string ServerIP;
            [DataMember]
            public int ServerPort;

            public ExtensionDataObject ExtensionData { get; set; }
        }

        private ClientData appData;
        public ServerConnection Server { get; private set; }
        public Task CommTask { get; private set; }

        public Model()
        {
            LoadAppData();

            // テスト用
            appData.ServerIP = "localhost";
            appData.ServerPort = 35224;

            Server = new ServerConnection(this, askServerAddress);
            CommTask = Server.Start();
        }


        #region ServerIP変更通知プロパティ
        public string ServerIP
        {
            get
            { return appData.ServerIP; }
            set
            {
                if (appData.ServerIP == value)
                    return;
                appData.ServerIP = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ServerPort変更通知プロパティ
        public int ServerPort
        {
            get
            { return appData.ServerPort; }
            set
            {
                if (appData.ServerPort == value)
                    return;
                appData.ServerPort = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region CurrentLogFile変更通知プロパティ
        private string _CurrentLogFile;

        public string CurrentLogFile
        {
            get
            { return _CurrentLogFile; }
            set
            { 
                if (_CurrentLogFile == value)
                    return;
                _CurrentLogFile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogItems変更通知プロパティ
        private ObservableCollection<LogItem> _LogItems = new ObservableCollection<LogItem>();

        public ObservableCollection<LogItem> LogItems
        {
            get
            { return _LogItems; }
            set
            { 
                if (_LogItems == value)
                    return;
                _LogItems = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region QueueItems変更通知プロパティ
        private ObservableCollection<QueueItem> _QueueItems;

        public ObservableCollection<QueueItem> QueueItems
        {
            get
            { return _QueueItems; }
            set
            { 
                if (_QueueItems == value)
                    return;
                _QueueItems = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region CurrentOperationResult変更通知プロパティ
        private string _CurrentOperationResult;

        public string CurrentOperationResult
        {
            get
            { return _CurrentOperationResult; }
            set
            { 
                if (_CurrentOperationResult == value)
                    return;
                _CurrentOperationResult = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsPaused変更通知プロパティ
        private bool _IsPaused;

        public bool IsPaused
        {
            get
            { return _IsPaused; }
            set
            { 
                if (_IsPaused == value)
                    return;
                _IsPaused = value;
                RaisePropertyChanged();
            }
        }
        #endregion


        public void Finish()
        {
            if (Server != null)
            {
                Server.Finish();
                Server = null;
            }
        }

        private void askServerAddress(string reason)
        {
            Console.WriteLine(reason);
            Thread.Sleep(1000);
            Server.SetServerAddress(appData.ServerIP, appData.ServerPort);
        }

        private string GetSettingFilePath()
        {
            return "AmatsukazeClient.xml";
        }

        private void LoadAppData()
        {
            if (File.Exists(GetSettingFilePath()) == false)
            {
                appData = new ClientData();
                return;
            }
            using (FileStream fs = new FileStream(GetSettingFilePath(), FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(ClientData));
                appData = (ClientData)s.ReadObject(fs);
            }
        }

        private void SaveAppData()
        {
            using (FileStream fs = new FileStream(GetSettingFilePath(), FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(ClientData));
                s.WriteObject(fs, appData);
            }
        }

        public Task OnConsole(string str)
        {
            // TODO:
            Debug.Print(str);
            return Task.FromResult(0);
        }

        public Task OnConsoleUpdate(string str)
        {
            // TODO:
            Debug.Print(str);
            return Task.FromResult(0);
        }

        public Task OnLogData(LogData data)
        {
            LogItems.Clear();
            foreach (var item in data.Items)
            {
                LogItems.Add(item);
            }
            return Task.FromResult(0);
        }

        public Task OnLogFile(string str)
        {
            CurrentLogFile = str;
            return Task.FromResult(0);
        }

        public Task OnLogUpdate(LogItem newLog)
        {
            LogItems.Add(newLog);
            return Task.FromResult(0);
        }

        public Task OnOperationResult(string result)
        {
            CurrentOperationResult = result;
            return Task.FromResult(0);
        }

        public Task OnQueueData(QueueData data)
        {
            QueueItems.Clear();
            foreach (var item in data.Items)
            {
                QueueItems.Add(item);
            }
            return Task.FromResult(0);
        }

        public Task OnQueueUpdate(QueueUpdate update)
        {
            if (update.AddOrRemove)
            {
                QueueItems.Add(update.Item);
            }
            else
            {
                QueueItems.Remove(update.Item);
            }
            return Task.FromResult(0);
        }

        public Task OnState(State state)
        {
            IsPaused = state.Pause;
            return Task.FromResult(0);
        }
    }
}
