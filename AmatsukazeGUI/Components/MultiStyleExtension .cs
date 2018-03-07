using System;
using System.Windows;
using System.Windows.Markup;

namespace Amatsukaze.Components
{
    [MarkupExtensionReturnType(typeof(Style))]
    public class MultiStyleExtension : MarkupExtension
    {
        private string[] resourceKeys;

        /// <summary>
        /// Public constructor.
        /// </summary>
        /// <param name="inputResourceKeys">The constructor input should be a string consisting of one or more style names separated by spaces.</param>
        public MultiStyleExtension(string inputResourceKeys)
        {
            if (inputResourceKeys == null)
                throw new ArgumentNullException("inputResourceKeys");
            this.resourceKeys = inputResourceKeys.Split(new char[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
            if (this.resourceKeys.Length == 0)
                throw new ArgumentException("No input resource keys specified.");
        }

        /// <summary>
        /// Returns a style that merges all styles with the keys specified in the constructor.
        /// </summary>
        /// <param name="serviceProvider">The service provider for this markup extension.</param>
        /// <returns>A style that merges all styles with the keys specified in the constructor.</returns>
        public override object ProvideValue(IServiceProvider serviceProvider)
        {
            Style resultStyle = new Style();
            foreach (string currentResourceKey in resourceKeys)
            {
                object key = currentResourceKey;
                if (currentResourceKey == ".")
                {
                    IProvideValueTarget service = (IProvideValueTarget)serviceProvider.GetService(typeof(IProvideValueTarget));
                    if (service.TargetObject is Style)
                    {
                        key = ((Style)service.TargetObject).TargetType;
                    }
                    else
                    {
                        key = service.TargetObject.GetType();
                    }
                }
                Style currentStyle = new StaticResourceExtension(key).ProvideValue(serviceProvider) as Style;
                if (currentStyle == null)
                    throw new InvalidOperationException("Could not find style with resource key " + currentResourceKey + ".");
                resultStyle.Merge(currentStyle);
            }
            return resultStyle;
        }
    }

    public static class MultiStyleMethods
    {
        /// <summary>
        /// Merges the two styles passed as parameters. The first style will be modified to include any 
        /// information present in the second. If there are collisions, the second style takes priority.
        /// </summary>
        /// <param name="style1">First style to merge, which will be modified to include information from the second one.</param>
        /// <param name="style2">Second style to merge.</param>
        public static void Merge(this Style style1, Style style2)
        {
            if (style1 == null)
                throw new ArgumentNullException("style1");
            if (style2 == null)
                throw new ArgumentNullException("style2");
            if (style1.TargetType.IsAssignableFrom(style2.TargetType))
                style1.TargetType = style2.TargetType;
            if (style2.BasedOn != null)
                Merge(style1, style2.BasedOn);
            foreach (SetterBase currentSetter in style2.Setters)
                style1.Setters.Add(currentSetter);
            foreach (TriggerBase currentTrigger in style2.Triggers)
                style1.Triggers.Add(currentTrigger);
            // This code is only needed when using DynamicResources.
            foreach (object key in style2.Resources.Keys)
                style1.Resources[key] = style2.Resources[key];
        }
    }
}
