using System.Windows;
using System.Windows.Interactivity;
using System.Windows.Shell;

namespace Amatsukaze.Components
{
    public class TaskbarInfoBehavior : Behavior<Window>
    {
        public TaskbarItemProgressState ProgressState {
            get { return (TaskbarItemProgressState)GetValue(StateProperty); }
            set { SetValue(StateProperty, value); }
        }

        // Using a DependencyProperty as the backing store for ProgressState.  This enables animation, styling, binding, etc...
        public static readonly DependencyProperty StateProperty =
            DependencyProperty.Register("ProgressState",
                typeof(TaskbarItemProgressState), typeof(TaskbarInfoBehavior),
                new UIPropertyMetadata(TaskbarItemProgressState.None, OnStateChanged));

        public double ProgressValue {
            get { return (double)GetValue(ValueProperty); }
            set { SetValue(ValueProperty, value); }
        }

        // Using a DependencyProperty as the backing store for ProgressValue.  This enables animation, styling, binding, etc...
        public static readonly DependencyProperty ValueProperty =
            DependencyProperty.Register("ProgressValue",
                typeof(double), typeof(TaskbarInfoBehavior), 
                new UIPropertyMetadata(0.0, OnValueChanged));

        protected override void OnAttached()
        {
            base.OnAttached();

            if(AssociatedObject.TaskbarItemInfo == null)
            {
                AssociatedObject.TaskbarItemInfo = new TaskbarItemInfo()
                {
                    ProgressState = ProgressState,
                    ProgressValue = ProgressValue
                };
            }
        }

        private static void OnStateChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {

            (d as TaskbarInfoBehavior).AssociatedObject.TaskbarItemInfo.ProgressState = 
                (TaskbarItemProgressState)e.NewValue;
        }

        private static void OnValueChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            (d as TaskbarInfoBehavior).AssociatedObject.TaskbarItemInfo.ProgressValue =
                (double)e.NewValue;
        }
    }
}
