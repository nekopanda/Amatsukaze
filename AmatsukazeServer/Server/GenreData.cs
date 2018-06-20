using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{

    public class SpaceGenre
    {
        public GenreSpace Space { get; set; }
        public string Name { get; set; }
        public SortedList<int, MainGenre> MainGenres { get; set; }
    }

    public class MainGenre
    {
        public string Name { get; set; }
        public GenreItem Item { get; set; }
        public SortedList<int, SubGenre> SubGenres { get; set; }

        public static MainGenre GetFromItem(GenreItem a)
        {
            SpaceGenre top;
            if (SubGenre.GENRE_TABLE.TryGetValue(a.Space, out top) == false)
            {
                return null;
            }
            MainGenre main;
            if (top.MainGenres.TryGetValue(a.Level1, out main) == false)
            {
                return null;
            }
            return main;
        }

        public static string GetUnknownName(GenreItem a)
        {
            return "不明" + (a.Space == (int)GenreSpace.CS ? "CS" : "") + "(" + a.Level1 + ")";
        }
    }

    public class SubGenre
    {
        public MainGenre Main { get; set; }
        public string Name { get; set; }
        public GenreItem Item { get; set; }

        // キーはGenreSpace
        public static readonly SortedList<int, SpaceGenre> GENRE_TABLE;

        static SubGenre()
        {
            var data =
            new[]
            {
                new
                {
                    Space = GenreSpace.ARIB,
                    Name = "",
                    Table = new []
                    {
                        new
                        {
                            Name = "ニュース／報道",
                            Table = new string[16]
                            {
                                "定時・総合",
                                "天気",
                                "特集・ドキュメント",
                                "政治・国会",
                                "経済・市況",
                                "海外・国際",
                                "解説",
                                "討論・会談",
                                "報道特番",
                                "ローカル・地域",
                                "交通",
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "スポーツ",
                            Table = new string[16]
                            {
                                "スポーツニュース",
                                "野球",
                                "サッカー",
                                "ゴルフ",
                                "その他の球技",
                                "相撲・格闘技",
                                "オリンピック・国際大会",
                                "マラソン・陸上・水泳",
                                "モータースポーツ",
                                "マリン・ウィンタースポーツ",
                                "競馬・公営競技",
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "情報／ワイドショー",
                            Table = new string[16]
                            {
                                "芸能・ワイドショー",
                                "ファッション",
                                "暮らし・住まい",
                                "健康・医療",
                                "ショッピング・通販",
                                "グルメ・料理",
                                "イベント",
                                "番組紹介・お知らせ",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "ドラマ",
                            Table = new string[16]
                            {
                                "国内ドラマ",
                                "海外ドラマ",
                                "時代劇",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "音楽",
                            Table = new string[16]
                            {
                                "国内ロック・ポップス",
                                "海外ロック・ポップス",
                                "クラシック・オペラ",
                                "ジャズ・フュージョン",
                                "歌謡曲・演歌",
                                "ライブ・コンサート",
                                "ランキング・リクエスト",
                                "カラオケ・のど自慢",
                                "民謡・邦楽",
                                "童謡・キッズ",
                                "民族音楽・ワールドミュージック",
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "バラエティ",
                            Table = new string[16]
                            {
                                "クイズ",
                                "ゲーム",
                                "トークバラエティ",
                                "お笑い・コメディ",
                                "音楽バラエティ",
                                "旅バラエティ",
                                "料理バラエティ",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "映画",
                            Table = new string[16]
                            {
                                "洋画",
                                "邦画",
                                "アニメ",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "アニメ／特撮",
                            Table = new string[16]
                            {
                                "国内アニメ",
                                "海外アニメ",
                                "特撮",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "ドキュメンタリー／教養",
                            Table = new string[16]
                            {
                                "社会・時事",
                                "歴史・紀行",
                                "自然・動物・環境",
                                "宇宙・科学・医学",
                                "カルチャー・伝統文化",
                                "文学・文芸",
                                "スポーツ",
                                "ドキュメンタリー全般",
                                "インタビュー・討論",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "劇場／公演",
                            Table = new string[16]
                            {
                                "現代劇・新劇",
                                "ミュージカル",
                                "ダンス・バレエ",
                                "落語・演芸",
                                "歌舞伎・古典",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "趣味／教育",
                            Table = new string[16]
                            {
                                "旅・釣り・アウトドア",
                                "園芸・ペット・手芸",
                                "音楽・美術・工芸",
                                "囲碁・将棋",
                                "麻雀・パチンコ",
                                "車・オートバイ",
                                "コンピュータ・ＴＶゲーム",
                                "会話・語学",
                                "幼児・小学生",
                                "中学生・高校生",
                                "大学生・受験",
                                "生涯教育・資格",
                                "教育問題",
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "福祉",
                            Table = new string[16]
                            {
                                "高齢者",
                                "障害者",
                                "社会福祉",
                                "ボランティア",
                                "手話",
                                "文字（字幕）",
                                "音声解説",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                    }
                },
                new
                {
                    Space = GenreSpace.CS,
                    Name = "CS",
                    Table = new []
                    {
                        new
                        {
                            Name = "スポーツ(CS)",
                            Table = new string[16]
                            {
                                "テニス",
                                "バスケットボール",
                                "ラグビー",
                                "アメリカンフットボール",
                                "ボクシング",
                                "プロレス",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "洋画(CS)",
                            Table = new string[16]
                            {
                                "アクション",
                                "SF／ファンタジー",
                                "コメディー",
                                "サスペンス／ミステリー",
                                "恋愛／ロマンス",
                                "ホラー／スリラー",
                                "ウエスタン",
                                "ドラマ／社会派ドラマ",
                                "アニメーション",
                                "ドキュメンタリー",
                                "アドベンチャー／冒険",
                                "ミュージカル／音楽映画",
                                "ホームドラマ",
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "邦画(CS)",
                            Table = new string[16]
                            {
                                "アクション",
                                "SF／ファンタジー",
                                "お笑い／コメディー",
                                "サスペンス／ミステリー",
                                "恋愛／ロマンス",
                                "ホラー／スリラー",
                                "青春／学園／アイドル",
                                "任侠／時代劇",
                                "アニメーション",
                                "ドキュメンタリー",
                                "アドベンチャー／冒険",
                                "ミュージカル／音楽映画",
                                "ホームドラマ",
                                null,
                                null,
                                "その他"
                            }
                        },
                        new
                        {
                            Name = "アダルト(CS)",
                            Table = new string[16]
                            {
                                "アダルト",
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                null,
                                "その他"
                            }
                        },
                    }
                }
            };

            GENRE_TABLE = new SortedList<int, SpaceGenre>();
            foreach (var a in data)
            {
                var spaceData = new SpaceGenre()
                {
                    Space = a.Space,
                    Name = a.Name,
                    MainGenres = new SortedList<int, MainGenre>()
                };
                for (int l1 = 0; l1 < a.Table.Length; ++l1)
                {
                    var b = a.Table[l1];
                    var l1Data = new MainGenre()
                    {
                        Item = new GenreItem()
                        {
                            Space = (int)a.Space,
                            Level1 = l1,
                            Level2 = -1
                        },
                        Name = b.Name,
                        SubGenres = new SortedList<int, SubGenre>()
                    };
                    for (int l2 = 0; l2 < b.Table.Length; ++l2)
                    {
                        if (b.Table[l2] != null)
                        {
                            var c = new SubGenre()
                            {
                                Main = l1Data,
                                Name = b.Table[l2],
                                Item = new GenreItem()
                                {
                                    Space = (int)a.Space,
                                    Level1 = l1,
                                    Level2 = l2
                                }
                            };
                            l1Data.SubGenres.Add(l2, c);
                        }
                    }
                    spaceData.MainGenres.Add(l1, l1Data);
                }
                GENRE_TABLE.Add((int)a.Space, spaceData);
            }

            // その他 - その他 を追加
            var other = new MainGenre()
            {
                Item = new GenreItem()
                {
                    Space = (int)GenreSpace.ARIB,
                    Level1 = 0xF,
                    Level2 = -1
                },
                Name = "その他",
                SubGenres = new SortedList<int, SubGenre>()
            };
            other.SubGenres.Add(0xF,
                new SubGenre()
                {
                    Main = other,
                    Name = "その他",
                    Item = new GenreItem()
                    {
                        Space = (int)GenreSpace.ARIB,
                        Level1 = 0xF,
                        Level2 = 0xF
                    }
                });
            GENRE_TABLE[(int)GenreSpace.ARIB].MainGenres.Add(0xF, other);
        }

        public static SubGenre GetDisplayGenre(GenreItem item)
        {
            if (GENRE_TABLE.ContainsKey(item.Space))
            {
                var space = GENRE_TABLE[item.Space];
                if (space.MainGenres.ContainsKey(item.Level1))
                {
                    var main = space.MainGenres[item.Level1];
                    if (main.SubGenres.ContainsKey(item.Level2))
                    {
                        return main.SubGenres[item.Level2];
                    }
                }
            }
            return new SubGenre()
            {
                Name = "不明",
                Item = item
            };
        }

        public string FullName {
            get {
                return Main.Name + " - " + Name;
            }
        }

        public static string GetUnknownFullName(GenreItem a)
        {
            string space;
            if(a.Space == (int)GenreSpace.ARIB)
            {
                space = "ARIB";
            }
            else if(a.Space == (int)GenreSpace.CS)
            {
                space = "CS";
            }
            else
            {
                space = "事業者定義(" + (a.Space - 1) + ")";
            }
            return "不明な" + space + "ジャンル(" + a.Level1 + "-" + a.Level2 + ")";
        }

        public override string ToString()
        {
            return Name;
        }

        public static bool IsInclude(GenreItem g, GenreItem sub)
        {
            if (g.Space != sub.Space || g.Level1 != sub.Level1) return false;
            if (g.Level2 != -1 && g.Level2 != sub.Level2) return false;
            return true;
        }

        public static SubGenre GetFromItem(GenreItem a)
        {
            SpaceGenre top;
            if (GENRE_TABLE.TryGetValue(a.Space, out top) == false)
            {
                return null;
            }
            MainGenre main;
            if (top.MainGenres.TryGetValue(a.Level1, out main) == false)
            {
                return null;
            }
            SubGenre sub;
            if (main.SubGenres.TryGetValue(a.Level2, out sub) == false)
            {
                return null;
            }
            return sub;
        }
    }
}
