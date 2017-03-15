using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.Serialization;
using System.Threading.Tasks;
using System.Threading;
using System.IO;

using Livet;
using Amatsukaze.Server;
using System.Collections.ObjectModel;

namespace Amatsukaze.Models
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
        public IEncodeServer Server { get; private set; }
        public Task CommTask { get; private set; }
        private ConsoleText consoleText;
        private Setting setting = new Setting();
        private State state = new State();

        public Func<object, string, Task> ServerAddressRequired;

        public string ServerIP
        {
            get { return appData.ServerIP; }
        }

        public int ServerPort
        {
            get { return appData.ServerPort; }
        }

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
        public bool IsPaused
        {
            get { return state.Pause; }
            set
            { 
                if (state.Pause == value)
                    return;
                state.Pause = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsRunning変更通知プロパティ
        public bool IsRunning {
            get { return state.Running; }
            set { 
                if (state.Running == value)
                    return;
                state.Running = value;
                RaisePropertyChanged();
                RaisePropertyChanged("IsRunningTest");
            }
        }

        public string IsRunningTest {
            get { return IsRunning ? "実行中" : "停止"; }
        }
        #endregion

        #region ConsoleTextLines変更通知プロパティ
        private ObservableCollection<string> _ConsoleTextLines = new ObservableCollection<string>();

        public ObservableCollection<string> ConsoleTextLines {
            get { return _ConsoleTextLines; }
            set { 
                if (_ConsoleTextLines == value)
                    return;
                _ConsoleTextLines = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X264Path変更通知プロパティ
        public string X264Path {
            get { return setting.X264Path; }
            set { 
                if (setting.X264Path == value)
                    return;
                setting.X264Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X265Path変更通知プロパティ
        public string X265Path {
            get { return setting.X265Path; }
            set { 
                if (setting.X265Path == value)
                    return;
                setting.X265Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region QSVEncPath変更通知プロパティ
        public string QSVEncPath {
            get { return setting.QSVEncPath; }
            set { 
                if (setting.QSVEncPath == value)
                    return;
                setting.QSVEncPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EncoderName変更通知プロパティ
        public string EncoderName {
            get { return setting.EncoderName; }
            set { 
                if (setting.EncoderName == value)
                    return;
                setting.EncoderName = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EncoderOption変更通知プロパティ
        public string EncoderOption {
            get { return setting.EncoderOption; }
            set { 
                if (setting.EncoderOption == value)
                    return;
                setting.EncoderOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MuxerPath変更通知プロパティ
        public string MuxerPath {
            get { return setting.MuxerPath; }
            set { 
                if (setting.MuxerPath == value)
                    return;
                setting.MuxerPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TimelineEditorPath変更通知プロパティ
        public string TimelineEditorPath {
            get { return setting.TimelineEditorPath; }
            set { 
                if (setting.TimelineEditorPath == value)
                    return;
                setting.TimelineEditorPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region WorkPath変更通知プロパティ
        public string WorkPath {
            get { return setting.WorkPath; }
            set { 
                if (setting.WorkPath == value)
                    return;
                setting.WorkPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public Model()
        {
            LoadAppData();

            // テスト用
            appData.ServerIP = "localhost";
            appData.ServerPort = 35224;

            consoleText = new ConsoleText(_ConsoleTextLines, 400);
        }

        public void Start()
        {
            if (App.Option.LaunchType == LaunchType.Standalone)
            {
                Server = new EncodeServer(0, this);
            }
            else
            {
                var connection = new ServerConnection(this, AskServerAddress);
                CommTask = connection.Start();
                Server = connection;
            }
        }

        public void SetServerAddress(string serverIp, int port)
        {
            appData.ServerIP = serverIp;
            appData.ServerPort = port;
            if (Server is ServerConnection)
            {
                (Server as ServerConnection).SetServerAddress(serverIp, port);
            }
        }

        public void Finish()
        {
            if (Server != null)
            {
                Server.Finish();
                Server = null;
            }
        }

        private bool firstAsked = true;
        private void AskServerAddress(string reason)
        {
            if (firstAsked)
            {
                (Server as ServerConnection).SetServerAddress(appData.ServerIP, appData.ServerPort);
                firstAsked = false;
            }
            else
            {
                ServerAddressRequired(this, reason);
            }
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

        public Task OnSetting(Setting setting)
        {
            X264Path = setting.X264Path;
            X265Path = setting.X265Path;
            QSVEncPath = setting.QSVEncPath;
            EncoderName = setting.EncoderName;
            EncoderOption = setting.EncoderOption;
            MuxerPath = setting.MuxerPath;
            TimelineEditorPath = setting.TimelineEditorPath;
            WorkPath = setting.WorkPath;
            return Task.FromResult(0);
        }

        public Task OnConsole(List<string> str)
        {
            consoleText.Clear();
            foreach(var s in str)
            {
                _ConsoleTextLines.Add(s);
            }
            return Task.FromResult(0);
        }

        public Task OnConsoleUpdate(byte[] str)
        {
            consoleText.AddBytes(str, 0, str.Length);
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
