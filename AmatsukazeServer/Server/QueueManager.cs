using Amatsukaze.Lib;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    class QueueManager
    {
        private EncodeServer server;

        public List<QueueItem> Queue { get; private set; } = new List<QueueItem>();

        class DirHash
        {
            public string DirPath;
            public Dictionary<string, byte[]> HashDict = new Dictionary<string, byte[]>();
        }

        private Dictionary<string, DirHash> hashCache = new Dictionary<string, DirHash>();

        private int nextItemId = 1;

        public QueueManager(EncodeServer server)
        {
            this.server = server;
        }

        public void LoadAppData()
        {
            string path = server.GetQueueFilePath();
            if (File.Exists(path) == false)
            {
                return;
            }
            using (FileStream fs = new FileStream(path, FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(List<QueueItem>));
                Queue = (List<QueueItem>)s.ReadObject(fs);
                // エンコード中だったのアイテムはリセットしておく
                foreach(var item in Queue)
                {
                    if(item.State == QueueState.Encoding)
                    {
                        item.State = QueueState.LogoPending;
                    }
                    if(item.Profile == null || item.Profile.LastUpdate == DateTime.MinValue)
                    {
                        item.Profile = server.PendingProfile;
                    }
                }
            }
        }

        public void SaveQueueData()
        {
            string path = server.GetQueueFilePath();
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            using (FileStream fs = new FileStream(path, FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(List<QueueItem>));
                s.WriteObject(fs, Queue);
            }
        }

        private Task NotifyMessage(bool fail, string message, bool log)
        {
            return server.NotifyMessage(fail, message, log);
        }

        private Task ClientQueueUpdate(QueueUpdate update)
        {
            return server.ClientQueueUpdate(update);
        }

        private void UpdateProgress()
        {
            // 進捗を更新
            double enabledCount = Queue.Count(s =>
                s.State != QueueState.LogoPending && s.State != QueueState.PreFailed);
            double remainCount = Queue.Count(s =>
                s.State == QueueState.Queue || s.State == QueueState.Encoding);
            // 完全にゼロだと分からないので・・・
            server.Progress = ((enabledCount - remainCount) + 0.1) / (enabledCount + 0.1);
        }

        public List<Task> UpdateQueueItems(List<Task> waits)
        {
            foreach (var item in Queue.ToArray())
            {
                if (item.State != QueueState.LogoPending && item.State != QueueState.Queue)
                {
                    continue;
                }
                if (UpdateQueueItem(item, waits))
                {
                    waits?.Add(NotifyQueueItemUpdate(item));
                }
            }
            return waits;
        }

        private bool CheckProfile(QueueItem item, List<Task> waits)
        {
            if (item.Profile != server.PendingProfile)
            {
                return true;
            }

            // ペンディングならプロファイルの決定を試みる
            int itemPriority = 0;
            var profile = server.SelectProfile(item, out itemPriority);
            if(profile == null)
            {
                return false;
            }

            item.Profile = ServerSupport.DeepCopy(profile);
            if (itemPriority > 0)
            {
                item.Priority = itemPriority;
            }

            waits?.Add(ClientQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Remove,
                Item = item
            }));

            return true;
        }

        // ペンディング <=> キュー 状態を切り替える
        // ペンディングからキューになったらスケジューリングに追加する
        // notifyItem: trueの場合は、ディレクトリ・アイテム両方の更新通知、falseの場合は、ディレクトリの更新通知のみ
        // 戻り値: 状態が変わった
        public bool UpdateQueueItem(QueueItem item, List<Task> waits)
        {
            if (item.State == QueueState.LogoPending || item.State == QueueState.Queue)
            {
                var prevState = item.State;
                if(item.Mode == ProcMode.DrcsCheck)
                {
                    // DRCSチェックはプロファイルを必要としないので即開始
                    if(item.State == QueueState.LogoPending)
                    {
                        item.FailReason = "";
                        item.State = QueueState.Queue;
                        server.ScheduleQueueItem(item);
                    }
                }
                else if (CheckProfile(item, waits))
                {
                    var map = server.ServiceMap;
                    if (item.ServiceId == -1)
                    {
                        item.FailReason = "TS情報取得中";
                        item.State = QueueState.LogoPending;
                    }
                    else if (map.ContainsKey(item.ServiceId) == false)
                    {
                        item.FailReason = "このTSに対する設定がありません";
                        item.State = QueueState.LogoPending;
                    }
                    else if (item.Profile.DisableChapter == false &&
                        map[item.ServiceId].LogoSettings.Any(s => s.CanUse(item.TsTime)) == false)
                    {
                        item.FailReason = "ロゴ設定がありません";
                        item.State = QueueState.LogoPending;
                    }
                    else
                    {
                        // OK
                        if (item.State == QueueState.LogoPending)
                        {
                            item.FailReason = "";
                            item.State = QueueState.Queue;

                            server.ScheduleQueueItem(item);
                        }
                    }
                }
                return prevState != item.State;
            }
            return false;
        }

        private AMTContext amtcontext = new AMTContext();
        public async Task AddQueue(AddQueueRequest req)
        {
            List<Task> waits = new List<Task>();

            // ユーザ操作でない場合はログを記録する
            bool enableLog = (req.Mode == ProcMode.AutoBatch);

            if (req.Outputs.Count == 0)
            {
                await NotifyMessage(true, "出力が1つもありません", enableLog);
                return;
            }

            // 既に追加されているファイルは除外する
            // バッチのときは全ファイルが対象だが、バッチじゃなければバッチのみが対象
            var ignores = req.IsBatch ? Queue : Queue.Where(t => t.IsBatch);

            var ignoreSet = new HashSet<string>(
                ignores.Where(item => item.IsActive)
                .Select(item => item.SrcPath));

            var items = ((req.Targets != null)
                ? req.Targets
                : Directory.GetFiles(req.DirPath)
                    .Where(s =>
                    {
                        string lower = s.ToLower();
                        return lower.EndsWith(".ts") || lower.EndsWith(".m2t");
                    })
                    .Select(f => new AddQueueItem() { Path = f }))
                    .Where(f => !ignoreSet.Contains(f.Path));

            var map = server.ServiceMap;
            var numItems = 0;

            // TSファイル情報を読む
            foreach (var additem in items)
            {
                using (var info = new TsInfo(amtcontext))
                {
                    var fileOK = false;
                    var failReason = "";
                    if (await Task.Run(() => info.ReadFile(additem.Path)) == false)
                    {
                        failReason = "TS情報取得に失敗: " + amtcontext.GetError();
                    }
                    // 1ソースファイルに対するaddはatomicに実行したいので、
                    // このスコープでは以降awaitしないこと
                    else
                    {
                        failReason = "TSファイルに映像が見つかりませんでした";
                        var list = info.GetProgramList();
                        var videopids = new List<int>();
                        int numFiles = 0;
                        for (int i = 0; i < list.Length; ++i)
                        {
                            var prog = list[i];
                            if (prog.HasVideo &&
                                videopids.Contains(prog.VideoPid) == false)
                            {
                                videopids.Add(prog.VideoPid);

                                var serviceName = "不明";
                                var tsTime = DateTime.MinValue;
                                if (info.HasServiceInfo)
                                {
                                    var service = info.GetServiceList().Where(s => s.ServiceId == prog.ServiceId).FirstOrDefault();
                                    if (service.ServiceId != 0)
                                    {
                                        serviceName = service.ServiceName;
                                    }
                                    tsTime = info.GetTime();
                                }

                                var outname = Path.GetFileNameWithoutExtension(additem.Path);
                                if (numFiles > 0)
                                {
                                    outname += "-マルチ" + numFiles;
                                }

                                Debug.Print("解析完了: " + additem.Path);

                                foreach (var outitem in req.Outputs)
                                {
                                    var genre = prog.Content.Select(s => ServerSupport.GetGenre(s)).ToList();

                                    var item = new QueueItem()
                                    {
                                        Id = nextItemId++,
                                        Mode = req.Mode,
                                        SrcPath = additem.Path,
                                        Hash = additem.Hash,
                                        DstPath = outitem.DstPath + "\\" + outname,
                                        ServiceId = prog.ServiceId,
                                        ImageWidth = prog.Width,
                                        ImageHeight = prog.Height,
                                        TsTime = tsTime,
                                        ServiceName = serviceName,
                                        EventName = prog.EventName,
                                        State = QueueState.LogoPending,
                                        Priority = outitem.Priority,
                                        AddTime = DateTime.Now,
                                        ProfileName = outitem.Profile,
                                        Genre = genre
                                    };

                                    if (item.IsOneSeg)
                                    {
                                        item.State = QueueState.PreFailed;
                                        item.FailReason = "映像が小さすぎます(" + prog.Width + "," + prog.Height + ")";
                                    }
                                    else
                                    {
                                        // ロゴファイルを探す
                                        if (req.Mode != ProcMode.DrcsCheck && map.ContainsKey(item.ServiceId) == false)
                                        {
                                            // 新しいサービスを登録
                                            waits.Add(server.AddService(new ServiceSettingElement()
                                            {
                                                ServiceId = item.ServiceId,
                                                ServiceName = item.ServiceName,
                                                LogoSettings = new List<LogoSetting>()
                                            }));
                                        }
                                        ++numFiles;
                                    }

                                    // プロファイルを設定
                                    UpdateProfileItem(item, null);
                                    // 追加
                                    Queue.Add(item);
                                    // まずは内部だけで状態を更新
                                    UpdateQueueItem(item, null);
                                    // 状態が決まったらクライアント側に追加通知
                                    waits.Add(ClientQueueUpdate(new QueueUpdate()
                                    {
                                        Type = UpdateType.Add,
                                        Item = item
                                    }));
                                    ++numItems;
                                    fileOK = true;
                                }
                            }
                        }

                    }

                    if (fileOK == false)
                    {
                        foreach (var outitem in req.Outputs)
                        {
                            bool isAuto = false;
                            var profileName = ServerSupport.ParseProfileName(outitem.Profile, out isAuto);
                            var profile = isAuto ? null : ServerSupport.DeepCopy(server.GetProfile(profileName));

                            var item = new QueueItem()
                            {
                                Id = nextItemId++,
                                Mode = req.Mode,
                                Profile = profile,
                                SrcPath = additem.Path,
                                Hash = additem.Hash,
                                DstPath = "",
                                ServiceId = -1,
                                ImageWidth = -1,
                                ImageHeight = -1,
                                TsTime = DateTime.MinValue,
                                ServiceName = "不明",
                                State = QueueState.PreFailed,
                                FailReason = failReason,
                                AddTime = DateTime.Now,
                                ProfileName = outitem.Profile,
                            };

                            Queue.Add(item);
                            waits.Add(ClientQueueUpdate(new QueueUpdate()
                            {
                                Type = UpdateType.Add,
                                Item = item
                            }));
                            ++numItems;
                        }
                    }

                    UpdateProgress();
                    waits.Add(server.RequestState());
                }
            }

            if (numItems == 0)
            {
                waits.Add(NotifyMessage(true,
                    "エンコード対象ファイルがありませんでした。パス:" + req.DirPath, enableLog));

                await Task.WhenAll(waits);

                return;
            }
            else
            {
                waits.Add(NotifyMessage(false, "" + numItems + "件追加しました", false));
            }

            if (req.Mode != ProcMode.AutoBatch)
            {
                // 最後に使った設定を記憶しておく
                server.LastUsedProfile = req.Outputs[0].Profile;
                server.LastOutputPath = req.Outputs[0].DstPath;
                waits.Add(server.RequestUIState());
            }

            waits.Add(server.RequestFreeSpace());

            await Task.WhenAll(waits);
        }

        private void ResetStateItem(QueueItem item, List<Task> waits)
        {
            item.State = QueueState.LogoPending;
            UpdateQueueItem(item, waits);
            waits.Add(NotifyQueueItemUpdate(item));
        }

        // アイテムのProfileNameからプロファイルを決定して、
        // オプションでwaits!=nullのときはクライアントに通知
        // 戻り値: プロファイルが変更された場合（結果、エラーになった場合も含む）
        private bool UpdateProfileItem(QueueItem item, List<Task> waits)
        {
            var getResult = server.GetProfile(item, item.ProfileName);
            var profile = (getResult != null) ? ServerSupport.DeepCopy(getResult.Profile) : server.PendingProfile;
            var priority = (getResult != null && getResult.Priority > 0) ? getResult.Priority : item.Priority;

            if(item.Profile == null ||
                item.Profile.Name != profile.Name ||
                item.Profile.LastUpdate != profile.LastUpdate ||
                item.Priority != priority)
            {
                // 変更
                item.Profile = profile;
                item.Priority = priority;

                // ハッシュリスト取得
                if (profile != server.PendingProfile && // ペンディングの場合は決定したときに実行される
                    item.Mode == ProcMode.Batch &&
                    profile.DisableHashCheck == false &&
                    item.FileName.StartsWith("\\\\"))
                {
                    var hashpath = Path.GetDirectoryName(item.FileName) + ".hash";
                    if(hashCache.ContainsKey(hashpath) == false)
                    {
                        if (File.Exists(hashpath) == false)
                        {
                            item.State = QueueState.PreFailed;
                            item.FailReason = "ハッシュファイルがありません: " + hashpath;
                            return true;
                        }
                        else
                        {
                            try
                            {
                                hashCache.Add(hashpath, new DirHash()
                                {
                                    DirPath = hashpath,
                                    HashDict = HashUtil.ReadHashFile(hashpath)
                                });
                            }
                            catch (IOException e)
                            {
                                item.State = QueueState.PreFailed;
                                item.FailReason = "ハッシュファイルの読み込みに失敗: " + e.Message;
                                return true;
                            }
                        }
                    }

                    var cacheItem = hashCache[hashpath];
                    var filename = Path.GetFileName(item.FileName);

                    if(cacheItem.HashDict.ContainsKey(filename) == false)
                    {
                        item.State = QueueState.PreFailed;
                        item.FailReason = "ハッシュファイルにこのファイルのハッシュがありません";
                        return true;
                    }

                    item.Hash = cacheItem.HashDict[filename];
                }

                UpdateQueueItem(item, waits);

                waits?.Add(ClientQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Add,
                    Item = item
                }));

                return true;
            }

            return false;
        }

        private void DuplicateItem(QueueItem item, List<Task> waits)
        {
            var newItem = ServerSupport.DeepCopy(item);
            newItem.Id = nextItemId++;
            Queue.Add(newItem);

            // 状態はリセットしておく
            newItem.State = QueueState.LogoPending;
            UpdateQueueItem(newItem, null);

            waits.Add(ClientQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Add,
                Item = newItem
            }));
        }

        internal Task NotifyQueueItemUpdate(QueueItem item)
        {
            UpdateProgress();
            if (Queue.Contains(item))
            {
                // ないアイテムをUpdateすると追加されてしまうので
                return Task.WhenAll(
                    ClientQueueUpdate(new QueueUpdate()
                    {
                        Type = UpdateType.Update,
                        Item = item
                    }),
                    server.RequestState());
            }
            return Task.FromResult(0);
        }

        private void RemoveCompleted(List<Task> waits)
        {
            var removeItems = Queue.Where(s => s.State == QueueState.Complete || s.State == QueueState.PreFailed).ToArray();
            foreach (var item in removeItems)
            {
                Queue.Remove(item);
                waits.Add(ClientQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Remove,
                    Item = item
                }));
            }
            waits.Add(NotifyMessage(false, "" + removeItems.Length + "件削除しました", false));
        }

        public Task ChangeItem(ChangeItemData data)
        {
            if (data.ChangeType == ChangeItemType.RemoveCompleted)
            {
                var waits = new List<Task>();
                RemoveCompleted(waits);
                return Task.WhenAll(waits);
            }

            // アイテム操作
            var target = Queue.FirstOrDefault(s => s.Id == data.ItemId);
            if (target == null)
            {
                return NotifyMessage(true,
                    "指定されたアイテムが見つかりません", false);
            }

            if (data.ChangeType == ChangeItemType.ResetState ||
                data.ChangeType == ChangeItemType.UpdateProfile ||
                data.ChangeType == ChangeItemType.Duplicate)
            {
                if (data.ChangeType == ChangeItemType.ResetState)
                {
                    // リトライは終わってるのだけ
                    if (target.State != QueueState.Complete &&
                        target.State != QueueState.Failed &&
                        target.State != QueueState.Canceled)
                    {
                        return NotifyMessage(true, "完了していないアイテムはリトライできません", false);
                    }
                }
                else if (data.ChangeType == ChangeItemType.UpdateProfile)
                {
                    // エンコード中は変更できない
                    if (target.State == QueueState.Encoding)
                    {
                        return NotifyMessage(true, "このアイテムはエンコード中のためプロファイル更新できません", false);
                    }
                }
                else if (data.ChangeType == ChangeItemType.Duplicate)
                {
                    // バッチモードでアクティブなやつは重複になるのでダメ
                    if (target.IsBatch && target.IsActive)
                    {
                        return NotifyMessage(true, "通常モードで追加されたアイテムは複製できません", false);
                    }
                }

                var waits = new List<Task>();

                if (target.IsBatch)
                {
                    // バッチモードでfailed/succeededフォルダに移動されていたら戻す
                    if (target.State == QueueState.Failed || target.State == QueueState.Complete)
                    {
                        if (Queue.Where(s => s.SrcPath == target.SrcPath).Any(s => s.IsActive) == false)
                        {
                            var dirPath = Path.GetDirectoryName(target.SrcPath);
                            var movedDir = (target.State == QueueState.Failed) ? 
                                ServerSupport.FAIL_DIR : 
                                ServerSupport.SUCCESS_DIR;
                            var movedPath = dirPath + "\\" + movedDir + "\\" + Path.GetFileName(target.SrcPath);
                            if (File.Exists(movedPath))
                            {
                                // EDCB関連ファイルも移動したかどうかは分からないが、あれば戻す
                                ServerSupport.MoveTSFile(movedPath, dirPath, true);
                            }
                        }
                    }
                }

                if (data.ChangeType == ChangeItemType.ResetState)
                {
                    ResetStateItem(target, waits);
                    waits.Add(NotifyMessage(false, "リトライします", false));
                }
                else if (data.ChangeType == ChangeItemType.UpdateProfile)
                {
                    if(UpdateProfileItem(target, waits))
                    {
                        waits.Add(NotifyMessage(false, "新しいプロファイルが適用されました", false));
                    }
                    else
                    {
                        waits.Add(NotifyMessage(false, "すでに最新のプロファイルが適用されています", false));
                    }
                }
                else
                {
                    DuplicateItem(target, waits);
                    waits.Add(NotifyMessage(false, "複製しました", false));
                }

                return Task.WhenAll(waits);
            }
            else if (data.ChangeType == ChangeItemType.Cancel)
            {
                if (target.IsActive)
                {
                    target.State = QueueState.Canceled;
                    return Task.WhenAll(
                        ClientQueueUpdate(new QueueUpdate()
                        {
                            Type = UpdateType.Update,
                            Item = target
                        }),
                        NotifyMessage(false, "キャンセルしました", false));
                }
                else
                {
                    return NotifyMessage(true,
                        "このアイテムはアクティブ状態でないため、キャンセルできません", false);
                }
            }
            else if (data.ChangeType == ChangeItemType.Priority)
            {
                target.Priority = data.Priority;
                server.UpdatePriority(target);
                return Task.WhenAll(
                    ClientQueueUpdate(new QueueUpdate()
                    {
                        Type = UpdateType.Update,
                        Item = target
                    }),
                    NotifyMessage(false, "優先度を変更しました", false));
            }
            else if (data.ChangeType == ChangeItemType.Profile)
            {
                if (target.State == QueueState.Encoding)
                {
                    return NotifyMessage(true, "エンコード中はプロファイル変更できません", false);
                }
                if (target.State == QueueState.PreFailed)
                {
                    return NotifyMessage(true, "このアイテムはプロファイル変更できません", false);
                }

                var waits = new List<Task>();
                target.ProfileName = data.Profile;
                if (UpdateProfileItem(target, waits))
                {
                    waits.Add(NotifyMessage(false, "プロファイルを「" + data.Profile + "」に変更しました", false));
                }
                else
                {
                    waits.Add(NotifyMessage(false, "既に同じプロファイルが適用されています", false));
                }

                return Task.WhenAll(waits);
            }
            else if (data.ChangeType == ChangeItemType.RemoveItem)
            {
                target.State = QueueState.Canceled;
                Queue.Remove(target);
                return Task.WhenAll(
                    ClientQueueUpdate(new QueueUpdate()
                    {
                        Type = UpdateType.Remove,
                        Item = target
                    }),
                    NotifyMessage(false, "アイテムを削除しました", false));
            }
            return Task.FromResult(0);
        }
    }
}
