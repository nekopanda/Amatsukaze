using System;
using System.Windows;
using System.Windows.Data;

namespace Amatsukaze.Components
{
    [ValueConversion(typeof(double), typeof(double))]
    public class NegateValueConverter : IValueConverter
    {
        private static object NegativeValue(object value)
        {
            if(value is double)
            {
                return -(double)value;
            }
            if(value is Thickness)
            {
                var t = (Thickness)value;
                return new Thickness(-t.Left, -t.Top, -t.Right, -t.Bottom);
            }
            throw new NotImplementedException("この型はサポートしていません: " + value.GetType().FullName);
        }

        #region IValueConverter Members

        public object Convert(object value, Type targetType, object parameter,
            System.Globalization.CultureInfo culture)
        {

            return NegativeValue(value);
        }

        public object ConvertBack(object value, Type targetType, object parameter,
            System.Globalization.CultureInfo culture)
        {
            return NegativeValue(value);
        }

        #endregion
    }
}
