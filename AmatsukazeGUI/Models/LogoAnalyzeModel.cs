using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

using Livet;
using System.Windows.Media.Imaging;
using System.IO;
using System.Windows;
using System.Threading.Tasks;

namespace Amatsukaze.Models
{
    public class LogoAnalyzeModel : NotificationObject
    {
        /*
         * NotificationObjectはプロパティ変更通知の仕組みを実装したオブジェクトです。
         */

        private AMTContext context;
        private MediaFile mediafile;

        private string logopath;

        #region CurrentImage変更通知プロパティ
        private BitmapSource _CurrentImage;

        public BitmapSource CurrentImage {
            get { return _CurrentImage; }
            set { 
                if (_CurrentImage == value)
                    return;
                _CurrentImage = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region FilePosition変更通知プロパティ
        private double _FilePosition = 0;

        public double FilePosition {
            get { return _FilePosition; }
            set { 
                if (_FilePosition == value)
                    return;
                _FilePosition = value;
                UpdateImage();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogoImage変更通知プロパティ
        private BitmapSource _LogoImage;

        public BitmapSource LogoImage {
            get { return _LogoImage; }
            set { 
                if (_LogoImage == value)
                    return;
                _LogoImage = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogoBG変更通知プロパティ
        private int _LogoBG;

        public int LogoBG {
            get { return _LogoBG; }
            set { 
                if (_LogoBG == value)
                    return;
                _LogoBG = value;
                UpdateLogoImage();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogoProgress変更通知プロパティ
        private double _LogoProgress;

        public double LogoProgress {
            get { return _LogoProgress; }
            set { 
                if (_LogoProgress == value)
                    return;
                _LogoProgress = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogoNumRead変更通知プロパティ
        private int _LogoNumRead;

        public int LogoNumRead {
            get { return _LogoNumRead; }
            set { 
                if (_LogoNumRead == value)
                    return;
                _LogoNumRead = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogoNumTotal変更通知プロパティ
        private int _LogoNumTotal;

        public int LogoNumTotal {
            get { return _LogoNumTotal; }
            set { 
                if (_LogoNumTotal == value)
                    return;
                _LogoNumTotal = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogoNumValid変更通知プロパティ
        private int _LogoNumValid;

        public int LogoNumValid {
            get { return _LogoNumValid; }
            set { 
                if (_LogoNumValid == value)
                    return;
                _LogoNumValid = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NowScanning変更通知プロパティ
        private bool _NowScanning;

        public bool NowScanning {
            get { return _NowScanning; }
            set { 
                if (_NowScanning == value)
                    return;
                _NowScanning = value;
                RaisePropertyChanged();
                RaisePropertyChanged("IsLogoScanEnabled");
            }
        }
        #endregion

        #region ShowProgress変更通知プロパティ
        private bool _ShowProgress;

        public bool ShowProgress {
            get { return _ShowProgress; }
            set { 
                if (_ShowProgress == value)
                    return;
                _ShowProgress = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Logo変更通知プロパティ
        private LogoFile _Logo;

        public LogoFile Logo {
            get { return _Logo; }
            set { 
                if (_Logo == value)
                    return;
                _Logo = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public bool CancelScanning = false;

        public LogoAnalyzeModel()
        {
            context = new AMTContext();
        }

        public void Load(string filepath)
        {
            if (mediafile != null) return;

            mediafile = new MediaFile(context, filepath);
            UpdateImage();
        }

        private void UpdateImage()
        {
            if (mediafile == null) return;

            var image = mediafile.GetFrame((float)_FilePosition);
            if(image != null)
            {
                CurrentImage = image;
            }
        }

        private bool LogoScanCallback(float progress, int nread, int total, int ngather)
        {
            LogoProgress = progress;
            LogoNumRead = nread;
            LogoNumTotal = total;
            LogoNumValid = ngather;
            return CancelScanning == false;
        }

        // 失敗するとIOExceptionが飛ぶ
        public async Task Analyze(string filepath, string workpath, Point pt, Size sz, int thy, int maxFrames)
        {
            int pid = System.Diagnostics.Process.GetCurrentProcess().Id;
            string workfile = workpath + "\\logotmp" + pid + ".dat";
            string tmppath = workpath + "\\logotmp" + pid + ".lgd";

            // 2の倍数にする
            int imgx = (int)Math.Floor(pt.X / 2) * 2;
            int imgy = (int)Math.Floor(pt.Y / 2) * 2;
            int w = (int)Math.Ceiling(sz.Width / 2) * 2;
            int h = (int)Math.Ceiling(sz.Height / 2) * 2;

            NowScanning = true;
            CancelScanning = false;

            ClearLogo();

            try
            {
                await Task.Run(() => LogoFile.ScanLogo(
                    context, filepath, workfile, tmppath, imgx, imgy, w, h, thy, maxFrames, LogoScanCallback));

                // TsInfoでサービス名を取得する
                var info = new TsInfo(context);
                info.ReadFile(filepath);
                var list = info.GetServiceList();

                // 名前を修正して保存し直す
                using(var logo = new LogoFile(context, tmppath))
                {
                    int serviceId = logo.ServiceId;
                    string serviceName = list.First(s => s.ServiceId == serviceId).ServiceName;
                    string date = info.GetTime().ToString("yyyy-MM-dd");
                    logo.Name = serviceName + "(" + date + ")";

                    logopath = workpath + "\\logo" + pid + ".lgd";
                    logo.Save(logopath);
                }

                Logo = new LogoFile(context, logopath);
                UpdateLogoImage();
            }
            finally
            {
                if(File.Exists(workfile))
                {
                    File.Delete(workfile);
                }
                if (File.Exists(tmppath))
                {
                    File.Delete(tmppath);
                }
                NowScanning = false;
            }
        }

        private void UpdateLogoImage()
        {
            if (Logo == null) return;

            var image = Logo.GetImage((byte)LogoBG);
            if (image != null)
            {
                LogoImage = image;
            }
        }

        // 失敗するとIOExceptionが飛ぶ
        public void CopyLogoFile()
        {
            string dirpath = "logo";
            Directory.CreateDirectory(dirpath);
            string prefix = dirpath + "\\SID" + Logo.ServiceId.ToString() + "-";
            for(int i = 1; i <= 1000; ++i)
            {
                string path = prefix + i + ".lgd";
                try
                {
                    File.Copy(logopath, path);
                    return;
                }
                catch(IOException) { }
            }
            throw new IOException("ロゴファイルをコピーできませんでした");
        }

        // ロゴを使い終わったら必ず呼ぶこと
        public void ClearLogo()
        {
            if(Logo != null)
            {
                File.Delete(logopath);
                logopath = null;
                Logo.Dispose();
                Logo = null;
            }
        }
    }
}
