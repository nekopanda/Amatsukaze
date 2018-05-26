using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace Amatsukaze.Server
{
    public class DataFile<T>
    {
        private string filepath;

        public DataFile(string filepath)
        {
            this.filepath = filepath;
        }

        public async Task<List<T>> Read()
        {
            if(!File.Exists(filepath))
            {
                return new List<T>();
            }
            var sb = new StringBuilder();
            sb.Append("<Root>");
            using (var reader = File.OpenText(filepath))
            {
                sb.Append(await reader.ReadToEndAsync());
            }
            sb.Append("</Root>");
            var list = new List<T>();
            await Task.Run((Action)(() =>
            {
                var s = new DataContractSerializer(typeof(T));
                using (var reader = XmlReader.Create(new StringReader(sb.ToString())))
                {
                    if (reader.Read())
                    {
                        reader.ReadStartElement();
                        while (reader.IsStartElement())
                        {
                            using (var subreader = reader.ReadSubtree())
                            {
                                list.Add((T)s.ReadObject(subreader));
                            }
                            reader.ReadEndElement();
                        }
                    }
                }
            }));
            return list;
        }

        public void Add(List<T> list)
        {
            var setting = new XmlWriterSettings() { OmitXmlDeclaration = true };
            var s = new DataContractSerializer(typeof(T));
            Directory.CreateDirectory(Path.GetDirectoryName(filepath));
            foreach(var item in list)
            {
                using(var fs = new FileStream(filepath, FileMode.Append))
                using (var writer = XmlWriter.Create(fs, setting))
                {
                    s.WriteObject(writer, item);
                }
            }
        }

        public void Delete()
        {
            if (File.Exists(filepath))
            {
                File.Delete(filepath);
            }
        }
    }
}
