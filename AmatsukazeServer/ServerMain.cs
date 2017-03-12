using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EncodeServer
{
    public class ServerMain
    {
        static void Main(string[] args)
        {
            EncodeServer server = new EncodeServer();
            UserClient client = new UserClient();

            Task.WaitAll(server.ServerTask, client.CommTask);
            Console.WriteLine("Finished");
        }
    }
}
