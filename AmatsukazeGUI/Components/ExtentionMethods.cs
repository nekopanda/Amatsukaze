using System.Collections.Generic;
using System.Linq;

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
