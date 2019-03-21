using System.ComponentModel;

namespace Amatsukaze.Components
{
    public partial class NotifyIconWrapper : Component
    {
        public System.Windows.Window Window;

        public string Text {
            get { return notifyIcon1.Text; }
            set { notifyIcon1.Text = value; }
        }

        public NotifyIconWrapper()
        {
            InitializeComponent();
        }

        public NotifyIconWrapper(IContainer container)
        {
            container.Add(this);

            InitializeComponent();
        }

        private void notifyIcon1_MouseClick(object sender, System.Windows.Forms.MouseEventArgs e)
        {
            if (Window == null) return;

            if (Window.WindowState == System.Windows.WindowState.Minimized)
            {
                Window.WindowState = System.Windows.WindowState.Normal;
            }
            Window.Activate();
        }
    }
}
