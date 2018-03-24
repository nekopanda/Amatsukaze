using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Components
{
    public static class ExtentionMethods
    {
        public static IEnumerable<T> OrEmpty<T>(this IEnumerable<T> collection)
        {
            return collection ?? Enumerable.Empty<T>();
        }
    }
}
