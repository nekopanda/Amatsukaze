using Amatsukaze.Lib;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    class QueueManager
    {
        private EncodeServer server;

        private List<QueueDirectory> queue = new List<QueueDirectory>();
        public List<QueueDirectory> Queue { get { return queue; } }

        private int nextDirId = 1;
        private int nextItemId = 1;

        public QueueManager(EncodeServer server)
        {
            this.server = server;
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
            var items = queue.SelectMany(t => t.Items);
            double enabledCount = items.Count(s =>
                s.State != QueueState.LogoPending && s.State != QueueState.PreFailed);
            double remainCount = items.Count(s =>
                s.State == QueueState.Queue || s.State == QueueState.Encoding);
            // 完全にゼロだと分からないので・・・
            server.Progress = ((enabledCount - remainCount) + 0.1) / (enabledCount + 0.1);
        }

        public List<Task> UpdateQueueItems(List<Task> waits)
        {
            foreach (var dir in queue.ToArray())
            {
                foreach (var item in dir.Items.ToArray())
                {
                    if (item.State != QueueState.LogoPending && item.State != QueueState.Queue)
                    {
                        continue;
                    }
                    if (UpdateQueueItem(item, waits, true))
                    {
                        waits.Add(NotifyQueueItemUpdate(item));
                    }
                }
            }
            return waits;
        }

        private bool CheckProfile(QueueItem item, QueueDirectory dir, List<Task> waits, bool notifyItem)
        {
            if (dir.Profile != server.PendingProfile)
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

            // プロファイルの選択ができたので、アイテムを適切なディレクトリに移動
            var newDir = GetQueueDirectory(dir.DirPath, dir.Mode, profile, waits);
            MoveItemDirectory(item, newDir, notifyItem ? waits : null);

            if (itemPriority > 0)
            {
                item.Priority = itemPriority;
            }

            return true;
        }

        private void MoveItemDirectory(QueueItem item, QueueDirectory newDir, List<Task> waits)
        {
            item.Dir.Items.Remove(item);

            // RemoveとAddをatomicに行わなければならないのでここをawaitにしないこと
            // ?.の後ろの引数はnullの場合は評価されないことに注意
            // （C#の仕様が分かりにくいけど・・・）
            waits?.Add(ClientQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Remove,
                DirId = item.Dir.Id,
                Item = item
            }));

            if (item.Dir.Profile == server.PendingProfile && item.Dir.Items.Count == 0)
            {
                // プロファイル未選択ディレクトリは自動的に削除する
                queue.Remove(item.Dir);
                waits?.Add(ClientQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Remove,
                    DirId = item.Dir.Id,
                }));
            }

            item.Dir = newDir;
            item.Dir.Items.Add(item);
            waits?.Add(ClientQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Add,
                DirId = item.Dir.Id,
                Item = item
            }));
        }

        private QueueDirectory GetQueueDirectory(string dirPath, ProcMode mode, ProfileSetting profile, List<Task> waitItems)
        {
            QueueDirectory target = queue.FirstOrDefault(s =>
                    s.DirPath == dirPath &&
                    s.Mode == mode &&
                    s.Profile.Name == profile.Name &&
                    s.Profile.LastUpdate == profile.LastUpdate);

            if (target == null)
            {
                var profilei = (profile == server.PendingProfile) ? profile : ServerSupport.DeepCopy(profile);
                target = new QueueDirectory()
                {
                    Id = nextDirId++,
                    DirPath = dirPath,
                    Items = new List<QueueItem>(),
                    Mode = mode,
                    Profile = profilei
                };

                // ハッシュリスト取得
                if (profile != server.PendingProfile && // ペンディングの場合は決定したときに実行される
                    mode == ProcMode.Batch &&
                    profile.DisableHashCheck == false &&
                    dirPath.StartsWith("\\\\"))
                {
                    var hashpath = dirPath + ".hash";
                    if (File.Exists(hashpath) == false)
                    {
                        waitItems.Add(NotifyMessage(true, "ハッシュファイルがありません: " + hashpath + "\r\n" +
                            "必要ない場合はハッシュチェックを無効化して再度追加してください", false));
                        return null;
                    }
                    try
                    {
                        target.HashList = HashUtil.ReadHashFile(hashpath);
                    }
                    catch (IOException e)
                    {
                        waitItems.Add(NotifyMessage(true, "ハッシュファイルの読み込みに失敗: " + e.Message, false));
                        return null;
                    }
                }

                queue.Add(target);
                waitItems.Add(ClientQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Add,
                    Directory = target
                }));
            }

            return target;
        }

        // ペンディング <=> キュー 状態を切り替える
        // ペンディングからキューになったらスケジューリングに追加する
        // notifyItem: trueの場合は、ディレクトリ・アイテム両方の更新通知、falseの場合は、ディレクトリの更新通知のみ
        // 戻り値: 状態が変わった
        public bool UpdateQueueItem(QueueItem item, List<Task> waits, bool notifyItem)
        {
            var dir = item.Dir;
            if (item.State == QueueState.LogoPending || item.State == QueueState.Queue)
            {
                var prevState = item.State;
                if(dir.Mode == ProcMode.DrcsCheck)
                {
                    // DRCSチェックはプロファイルを必要としないので即開始
                    if(item.State == QueueState.LogoPending)
                    {
                        item.FailReason = "";
                        item.State = QueueState.Queue;
                        server.ScheduleQueueItem(item);
                    }
                }
                else if (CheckProfile(item, dir, waits, notifyItem))
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
                    else if (dir.Profile.DisableChapter == false &&
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
        public async Task AddQueue(AddQueueDirectory dir)
        {
            List<Task> waits = new List<Task>();

            // ユーザ操作でない場合はログを記録する
            bool enableLog = (dir.Mode == ProcMode.AutoBatch);

            if (dir.Outputs.Count == 0)
            {
                await NotifyMessage(true, "出力が1つもありません", enableLog);
                return;
            }

            // 既に追加されているファイルは除外する
            var ignores = queue
                .Where(t => t.DirPath == dir.DirPath);

            // バッチのときは全ファイルが対象だが、バッチじゃなければバッチのみが対象
            if (!dir.IsBatch)
            {
                ignores = ignores.Where(t => t.IsBatch);
            }

            var ignoreSet = new HashSet<string>(
                ignores.SelectMany(t => t.Items)
                .Where(item => item.IsActive)
                .Select(item => item.SrcPath));

            var items = ((dir.Targets != null)
                ? dir.Targets
                : Directory.GetFiles(dir.DirPath)
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

                                var outname = Path.GetFileName(additem.Path);
                                if (numFiles > 0)
                                {
                                    outname = Path.GetFileNameWithoutExtension(outname) + "-マルチ" + numFiles;
                                }

                                Debug.Print("解析完了: " + additem.Path);

                                foreach (var outitem in dir.Outputs)
                                {
                                    var genre = prog.Content.Select(s => ServerSupport.GetGenre(s)).ToList();
                                    var profile = server.GetProfile(Path.GetFileName(additem.Path),
                                        prog.Width, prog.Height, genre, prog.ServiceId, outitem.Profile);
                                    var target = GetQueueDirectory(dir.DirPath, dir.Mode, profile?.Profile ?? server.PendingProfile, waits);
                                    var priority = (profile != null && profile.Priority > 0) ? profile.Priority : outitem.Priority;

                                    var item = new QueueItem()
                                    {
                                        Id = nextItemId++,
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
                                        Priority = priority,
                                        AddTime = DateTime.Now,
                                        ProfileName = outitem.Profile,
                                        Dir = target,
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
                                        if (dir.Mode != ProcMode.DrcsCheck && map.ContainsKey(item.ServiceId) == false)
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

                                    // 追加
                                    target.Items.Add(item);
                                    // まずは内部だけで状態を更新
                                    UpdateQueueItem(item, waits, false);
                                    // 状態が決まったらクライアント側に追加通知
                                    waits.Add(ClientQueueUpdate(new QueueUpdate()
                                    {
                                        Type = UpdateType.Add,
                                        DirId = item.Dir.Id,
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
                        foreach (var outitem in dir.Outputs)
                        {
                            bool isAuto = false;
                            var profileName = ServerSupport.ParseProfileName(outitem.Profile, out isAuto);
                            var profile = isAuto ? null : server.GetProfile(profileName);
                            var target = GetQueueDirectory(dir.DirPath, dir.Mode, profile ?? server.PendingProfile, waits);
                            var item = new QueueItem()
                            {
                                Id = nextItemId++,
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
                                Dir = target
                            };

                            target.Items.Add(item);
                            waits.Add(ClientQueueUpdate(new QueueUpdate()
                            {
                                Type = UpdateType.Add,
                                DirId = target.Id,
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
                    "エンコード対象ファイルがありませんでした。パス:" + dir.DirPath, enableLog));

                await Task.WhenAll(waits);

                return;
            }
            else
            {
                waits.Add(NotifyMessage(false, "" + numItems + "件追加しました", false));
            }

            if (dir.Mode != ProcMode.AutoBatch)
            {
                // 最後に使ったプロファイルを記憶しておく
                bool isAuto = false;
                var profileName = ServerSupport.ParseProfileName(dir.Outputs[0].Profile, out isAuto);
                if (!isAuto)
                {
                    server.LastSelectedProfile = profileName;
                }
            }

            waits.Add(server.RequestFreeSpace());

            await Task.WhenAll(waits);
        }

        private void ResetStateItem(QueueItem item, List<Task> waits)
        {
            item.State = QueueState.LogoPending;
            UpdateQueueItem(item, waits, true);
            waits.Add(NotifyQueueItemUpdate(item));
        }

        private void UpdateProfileItem(QueueItem item, List<Task> waits)
        {
            var profile = server.GetProfile(item, item.ProfileName);
            var newDir = GetQueueDirectory(item.Dir.DirPath, item.Dir.Mode, profile?.Profile ?? server.PendingProfile, waits);
            if (newDir != item.Dir)
            {
                MoveItemDirectory(item, newDir, waits);
            }
            if (profile != null && profile.Priority > 0)
            {
                item.Priority = profile.Priority;
            }
        }

        private void DuplicateItem(QueueItem item, List<Task> waits)
        {
            var newItem = ServerSupport.DeepCopy(item);
            newItem.Id = nextItemId++;
            newItem.Dir = item.Dir;
            newItem.Dir.Items.Add(newItem);

            // 状態はリセットしておく
            newItem.State = QueueState.LogoPending;
            UpdateQueueItem(newItem, waits, false);

            waits.Add(ClientQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Add,
                DirId = newItem.Dir.Id,
                Item = newItem
            }));
        }

        internal Task NotifyQueueItemUpdate(QueueItem item)
        {
            UpdateProgress();
            if (item.Dir.Items.Contains(item))
            {
                // ないアイテムをUpdateすると追加されてしまうので
                return Task.WhenAll(
                    ClientQueueUpdate(new QueueUpdate()
                    {
                        Type = UpdateType.Update,
                        DirId = item.Dir.Id,
                        Item = item
                    }),
                    server.RequestState());
            }
            return Task.FromResult(0);
        }

        private int RemoveCompleted(QueueDirectory dir, List<Task> waits)
        {
            if (dir.Items.All(s => s.State == QueueState.Complete || s.State == QueueState.PreFailed))
            {
                // 全て完了しているのでディレクトリを削除
                queue.Remove(dir);
                waits.Add(ClientQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Remove,
                    DirId = dir.Id
                }));
                return dir.Items.Count;
            }

            var removeItems = dir.Items.Where(s => s.State == QueueState.Complete).ToArray();
            foreach (var item in removeItems)
            {
                dir.Items.Remove(item);
                waits.Add(ClientQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Remove,
                    DirId = item.Dir.Id,
                    Item = item
                }));
            }
            return removeItems.Length;
        }

        public Task ChangeItem(ChangeItemData data)
        {
            if (data.ChangeType == ChangeItemType.RemoveCompletedAll)
            {
                // 全て対象
                var waits = new List<Task>();
                int removeItems = 0;
                foreach (var dir in queue.ToArray())
                {
                    removeItems += RemoveCompleted(dir, waits);
                }
                waits.Add(NotifyMessage(false, "" + removeItems + "件削除しました", false));
                return Task.WhenAll(waits);
            }
            else if (data.ChangeType == ChangeItemType.RemoveDir ||
                data.ChangeType == ChangeItemType.RemoveCompletedItem)
            {
                // ディレクトリ操作
                var target = queue.Find(t => t.Id == data.ItemId);
                if (target == null)
                {
                    return NotifyMessage(true,
                        "指定されたキューディレクトリが見つかりません", false);
                }

                if (data.ChangeType == ChangeItemType.RemoveDir)
                {
                    // ディレクトリ削除
                    queue.Remove(target);
                    // 全てキャンセル
                    foreach (var item in target.Items)
                    {
                        item.State = QueueState.Canceled;
                    }
                    return Task.WhenAll(
                        ClientQueueUpdate(new QueueUpdate()
                        {
                            Type = UpdateType.Remove,
                            DirId = target.Id
                        }),
                        NotifyMessage(false, "ディレクトリ「" + target.DirPath + "」を削除しました", false));
                }
                else if (data.ChangeType == ChangeItemType.RemoveCompletedItem)
                {
                    // ディレクトリの完了削除
                    var waits = new List<Task>();
                    int removeItems = RemoveCompleted(target, waits);
                    waits.Add(NotifyMessage(false, "" + removeItems + "件削除しました", false));
                    return Task.WhenAll(waits);
                }
            }
            else
            {
                // アイテム操作
                var all = queue.SelectMany(d => d.Items);
                var target = all.FirstOrDefault(s => s.Id == data.ItemId);
                if (target == null)
                {
                    return NotifyMessage(true,
                        "指定されたアイテムが見つかりません", false);
                }

                var dir = target.Dir;

                if (data.ChangeType == ChangeItemType.ResetState ||
                    data.ChangeType == ChangeItemType.UpdateProfile ||
                    data.ChangeType == ChangeItemType.Duplicate)
                {
                    if (data.ChangeType == ChangeItemType.ResetState)
                    {
                        // 状態リセットは終わってるのだけ
                        if (target.State != QueueState.Complete &&
                            target.State != QueueState.Failed &&
                            target.State != QueueState.Canceled)
                        {
                            return NotifyMessage(true, "完了していないアイテムは状態リセットできません", false);
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
                        if (target.Dir.IsBatch && target.IsActive)
                        {
                            return NotifyMessage(true, "通常モードで追加されたアイテムは複製できません", false);
                        }
                    }

                    var waits = new List<Task>();

                    if (dir.IsBatch)
                    {
                        // バッチモードでfailed/succeededフォルダに移動されていたら戻す
                        if (target.State == QueueState.Failed || target.State == QueueState.Complete)
                        {
                            if (all.Where(s => s.SrcPath == target.SrcPath).Any(s => s.IsActive) == false)
                            {
                                var movedDir = (target.State == QueueState.Failed) ? dir.Failed : dir.Succeeded;
                                var movedPath = movedDir + "\\" + Path.GetFileName(target.FileName);
                                if (File.Exists(movedPath))
                                {
                                    // EDCB関連ファイルも移動したかどうかは分からないが、あれば戻す
                                    ServerSupport.MoveTSFile(movedPath, dir.DirPath, true);
                                }
                            }
                        }
                    }

                    if (data.ChangeType == ChangeItemType.ResetState)
                    {
                        ResetStateItem(target, waits);
                        waits.Add(NotifyMessage(false, "状態リセットします", false));
                    }
                    else if (data.ChangeType == ChangeItemType.UpdateProfile)
                    {
                        UpdateProfileItem(target, waits);
                        waits.Add(NotifyMessage(false, "プロファイル再適用します", false));
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
                                DirId = target.Dir.Id,
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
                            DirId = target.Dir.Id,
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
                    var profile = server.GetProfile(target, target.ProfileName);
                    var newDir = GetQueueDirectory(target.Dir.DirPath, target.Dir.Mode, profile?.Profile ?? server.PendingProfile, waits);
                    if (newDir != target.Dir)
                    {
                        MoveItemDirectory(target, newDir, waits);
                        if (UpdateQueueItem(target, waits, true))
                        {
                            waits.Add(NotifyQueueItemUpdate(target));
                        }
                        waits.Add(NotifyMessage(false, "プロファイルを「" + data.Profile + "」に変更しました", false));
                    }

                    return Task.WhenAll(waits);
                }
                else if (data.ChangeType == ChangeItemType.RemoveItem)
                {
                    target.State = QueueState.Canceled;
                    dir.Items.Remove(target);
                    return Task.WhenAll(
                        ClientQueueUpdate(new QueueUpdate()
                        {
                            Type = UpdateType.Remove,
                            DirId = target.Dir.Id,
                            Item = target
                        }),
                        NotifyMessage(false, "アイテムを削除しました", false));
                }
            }
            return Task.FromResult(0);
        }
    }
}
