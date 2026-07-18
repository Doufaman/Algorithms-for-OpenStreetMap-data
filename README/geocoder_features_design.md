# Geocoder Features:任务与实现方案

## 项目概览

在 Jung et al. (2011) *Efficient Error-Correcting Geocoding* 的 baseline pipeline 之上,增加**两层查询规范化机制**,让 geocoder 能处理真实用户的输入变体——特别是英语键盘限制和非德语母语用户的场景。

两个 feature 相互独立、可组合、可 ablation 测试。

---

## Feature 1: Orthographic Normalization(正字法规范化)

### 目标

处理**同一个德语地名的多种合法或半合法书写形式**。用户想拼对,但键盘或输入习惯不允许。

### 覆盖范围

| 类别 | 示例输入 | 目标 canonical |
|---|---|---|
| 大小写 | `DÜSSELDORF`, `düsseldorf` | `düsseldorf` |
| Umlaut 官方替写(DIN 5007) | `Duesseldorf`, `Koeln`, `Muenchen` | 匹配 `Düsseldorf`, `Köln`, `München` |
| Umlaut 省略 | `Dusseldorf`, `Koln`, `Munchen` | 匹配 `Düsseldorf`, `Köln`, `München` |
| ß 替写 | `Strasse`, `Strabe` | 匹配 `Straße` |
| 街道后缀缩写 | `Bahnhofstr.`, `Str.` | 匹配 `Bahnhofstraße` |
| 其他后缀缩写 | `Marienpl.`, `Hbf.` | 匹配 `Marienplatz`, `Hauptbahnhof` |
| Unicode 符号 | `–` (en-dash), `\u00a0` (nbsp) | 归一化 |

### 实现方案

#### Step 1:定义两个规范化函数

```python
def normalize_din(s: str) -> str:
    """DIN 5007 官方替写"""
    s = s.lower()
    s = s.replace('ä', 'ae').replace('ö', 'oe')
    s = s.replace('ü', 'ue').replace('ß', 'ss')
    return s

def normalize_strip(s: str) -> str:
    """省略式替写(用户偷懒)"""
    s = s.lower()
    s = s.replace('ä', 'a').replace('ö', 'o')
    s = s.replace('ü', 'u').replace('ß', 's')
    return s
```

#### Step 2:Token 化时拆分后缀

参考 Jung et al. §3.1,把 `-straße`、`-platz`、`-str.`、`-pl.` 等后缀拆成独立 token。这样 `"Bahnhofstraße"` 和 `"Bahnhof Straße"` 在索引里等价。

需要一张后缀表:

```python
GERMAN_SUFFIXES = [
    "straße", "strasse", "str",
    "platz", "pl",
    "weg", "allee", "gasse", "ring", "damm", "ufer",
    "bahnhof", "hauptbahnhof",
    "brücke", "bruecke",
    # ...
]
```

#### Step 3:双索引(索引侧)

建索引时,每个 token 同时索引两种规范化形式,指向同一个实体 ID:

```python
def index_token(token, entity_id):
    forms = {normalize_din(token), normalize_strip(token)}
    for form in forms:
        inverted_index[form].add(entity_id)
```

对没有特殊字符的 token,两种规范化输出相同,只索引一份。对含 umlaut 的 token(约 10–15%),索引两份。**空间开销可忽略**。

#### Step 4:查询侧对称处理

```python
def lookup(query_token):
    return (inverted_index.get(normalize_din(query_token), set()) |
            inverted_index.get(normalize_strip(query_token), set()))
```

对近似索引(SymSpell 或 Jung et al. 用的 approximate dictionary)也用同样的两种规范形式建立。

#### Step 5:显示时保留原始形态

内部处理用规范化形式,但返回给用户的结果始终显示 canonical 德语名(带 umlaut 的原始形式)。

### 对应 Exercise Sheet 任务

- **Mandatory Task 1** (String Preprocessing) — 主
- **Optional Task 3** (Error correction) — 次(处理极端情况如 `Strabe` → `Straße` 时依赖 fuzzy 兜底)

---

## Feature 2: Semantic Query Interpretation(语义查询解释)

### 目标

处理**不懂德语的用户用英语词代替德语词**的搜索。用户根本不知道德语怎么说,或者只知道英语名。

分两层,处理不同类型的输入。

### Layer 2a — Exonyms(地名英德对照)

覆盖范围:约 30–50 对常见德国城市/州的英德对照。

| English exonym | German endonym |
|---|---|
| Munich | München |
| Cologne | Köln |
| Nuremberg | Nürnberg |
| Hanover | Hannover |
| Brunswick | Braunschweig |
| Bavaria | Bayern |
| Saxony | Sachsen |
| Hesse | Hessen |
| Thuringia | Thüringen |
| Westphalia | Westfalen |
| Black Forest | Schwarzwald |
| Rhine | Rhein |

**数据来源**:OSM PBF 文件的 `name:en` 标签(读 PBF 时顺带抽取),加手工补充。

**实现**:建索引时,如果一个 town/region 有 `name:en` 标签,把英文名也索引到同一个实体 ID。查询时不需要额外逻辑,倒排索引自动命中。

```python
def index_town(town_name, town_id, name_en=None):
    for tok in tokenize(town_name):
        index_token(tok, town_id)
    if name_en:
        for tok in tokenize(name_en):
            index_token(tok, town_id)  # 同一 ID,不同 alias
```

### Layer 2b — Category Word Synonyms(类别词同义)

覆盖范围:约 30 对常见 POI 类别词的英德对照,包括 multi-token 短语。

| German canonical | English synonyms |
|---|---|
| Hauptbahnhof | main station, central station, main train station, hbf |
| Bahnhof | station, train station |
| Flughafen | airport |
| Schloss | castle, palace |
| Dom | cathedral |
| Kirche | church |
| Rathaus | city hall, town hall |
| Brücke | bridge |
| Markt | market, market square |
| Platz | square, plaza |
| Straße | street, st |
| Weg | way, path |
| Allee | avenue, ave |

#### 实现:双路径查询

关键设计——**不在索引侧扩展**(避免污染 IDF 权重),而在**查询侧做同义词替换**,并同时保留原始查询,合并两条路径的结果:

```python
SYNONYM_BIGRAMS = {
    "main station":    "hauptbahnhof",
    "central station": "hauptbahnhof",
    "train station":   "bahnhof",
    "main train station": "hauptbahnhof",
    "city hall":       "rathaus",
    "town hall":       "rathaus",
    "market square":   "markt",
}

SYNONYM_UNIGRAMS = {
    "station":   "bahnhof",
    "airport":   "flughafen",
    "cathedral": "dom",
    "castle":    "schloss",
    "palace":    "schloss",
    "church":    "kirche",
    "market":    "markt",
    "square":    "platz",
    "plaza":     "platz",
    "bridge":    "bruecke",
    "hbf":       "hauptbahnhof",
    "street":    "strasse",
    "avenue":    "allee",
}

def apply_synonyms(tokens):
    """贪心 longest-match:优先 2-gram,回退 1-gram"""
    result, i = [], 0
    while i < len(tokens):
        if i + 1 < len(tokens):
            bg = f"{tokens[i]} {tokens[i+1]}"
            if bg in SYNONYM_BIGRAMS:
                result.append(SYNONYM_BIGRAMS[bg])
                i += 2
                continue
        result.append(SYNONYM_UNIGRAMS.get(tokens[i], tokens[i]))
        i += 1
    return result

def geocode(query):
    tokens = normalize_and_tokenize(query)  # Feature 1
    
    # 路径 A:原样查询
    results_a = jung_pipeline(tokens)
    
    # 路径 B:同义词替换后查询
    expanded = apply_synonyms(tokens)
    if expanded != tokens:
        results_b = jung_pipeline(expanded)
        return merge_and_rank(results_a, results_b)
    return results_a
```

**双路径的原因**:处理歧义(如 `"Frankfurt main station"` 既可能是 Frankfurt am Main 的车站,也可能是 Frankfurt 的主车站),让 Jung et al. 的 rating 函数决定哪个解读更好。

#### 关键前提:POI 数据必须进索引

上述机制能成功的前提是:索引里必须包含 `railway=station`、`amenity=*` 等 POI 实体。在读 PBF 时需要扩展索引范围:

```python
POI_TAGS = {
    "railway": {"station", "halt"},
    "amenity": {"restaurant", "pharmacy", "school", "town_hall", ...},
    "tourism": {"museum", "hotel", "attraction", ...},
    "aeroway": {"aerodrome"},
    "shop": True,  # 所有 shop
}
```

### 对应 Exercise Sheet 任务

- **Mandatory Task 3** (Heuristics) — 主(核心是"如何解释自然语言查询")
- **Optional Task 3** (Error correction) — 次(英语查询在德语数据中找不到时的挽救)
- **Optional Task 5** (Example Queries) — 演示("Stuttgart Burger King" 类品牌查询也走类似机制)

---

## 完整 Pipeline 示意

```
用户输入: "Munich main station"
    │
    ▼
┌─────────────────────────────────────┐
│  Feature 1: Orthographic Normalize  │
│  - Unicode 归一化                    │
│  - lowercase                        │
│  - 后缀拆分 (Str., Pl., Bhf.)        │
│  - Tokenize                         │
└─────────────────────────────────────┘
    │
    ▼
tokens: ["munich", "main", "station"]
    │
    ├──── 路径 A(原样)───────────────┐
    │                                │
    ▼                                │
┌─────────────────────────┐          │
│  Feature 2b: Synonym    │          │
│  Replacement (贪心)     │          │
└─────────────────────────┘          │
    │                                │
    ▼                                │
["münchen", "hauptbahnhof"]     ["munich", "main", "station"]
(通过 Feature 2a exonym)             │
    │                                │
    ▼                                ▼
┌─────────────────────────────────────┐
│  Jung et al. Pipeline (each path)   │
│  - Inverted index (with ILT)        │
│  - Approximate index (DL, d=2)      │
│  - Feature 1 双索引 (DIN + strip)   │
│  - Feature 2a Exonym 索引集成        │
│  - Compatible candidates (FIC)      │
│  - Bipartite token matching         │
│  - IDF-weighted rating              │
└─────────────────────────────────────┘
    │                                │
    ▼                                ▼
results_b (high rating)         results_a (low rating)
    │                                │
    └──────────── merge ─────────────┘
                    │
                    ▼
返回: München Hauptbahnhof
      (显示 canonical + "matched via 'main station' → Hauptbahnhof")
```

---

## 任务覆盖矩阵

| Exercise Sheet 任务 | Feature 1 | Feature 2a | Feature 2b |
|---|:---:|:---:|:---:|
| Mandatory 1 — String Preprocessing | 主 | 次 | 次 |
| Mandatory 2 — Geocoder (base) | 底层 | 底层 | 底层 |
| Mandatory 3 — Heuristics | | 主 | 主 |
| Mandatory 4 — Timings | 全部功能都要测 | | |
| Optional 3 — Error correction | 次 | 次 | 次 |
| Optional 5 — Example Queries | | 演示 | 演示 |

一个 feature 组合覆盖了 **Mandatory 1 + 3 + Optional 3 + 5** 四个任务。

---

## 关键设计决策(写进 README)

### 决策 1:两层规范化 vs. 一层大杂烩

把"正字法"和"语义"明确分成两层——前者是**确定性映射**(每个变体都可以静态映射到 canonical),后者是**歧义解释**(需要保留多种可能)。分层让代码可测试、可扩展、可解释。

### 决策 2:双路径查询而不是单路径替换

Feature 2 保留原始 token 序列的查询路径,是因为语义替换本质上是猜测。让 Jung et al. 的 rating 函数当仲裁,而不是提前做决定。

例:`"Frankfurt main station"`
- 路径 A 保留:能匹配 Frankfurt am Main(通过 IDF 高的 "Main" token 精确匹配)
- 路径 B 替换:能匹配 Frankfurt Hauptbahnhof

两条路径都返回,按 rating 排序,由数据本身决定哪个更可能。

### 决策 3:索引侧扩展 vs. 查询侧扩展

- **Feature 1**(确定性,低歧义):在**索引侧**扩展 — 增大索引一点点,但查询快
- **Feature 2a**(exonym,静态一对一):在**索引侧**扩展 — 无歧义,直接索引 alias
- **Feature 2b**(类别词,高歧义):在**查询侧**扩展 — 不污染 IDF 权重,双路径合并结果

### 决策 4:数据来源优先级

1. OSM PBF 的现有标签(`name`、`name:en`、`addr:*`)— 优先
2. 手工映射表 — 补充 PBF 覆盖不到的
3. 避免机翻 — 对类别词和 exonym 的精度不够

---

## 已知边界情况(future work 或明确不处理)

### Feature 1 的 collision

`"Aue"`(萨克森州的一个城市)在 strip 规范化下可能和 `"Aü"` 的名字碰撞。接受这个 trade-off——rating 阶段会把真正的 `"Aue"` 排到正确位置。

### Feature 2 的语义歧义

- `"Main"` 是英语 "main"(→ Haupt-)还是德语 Main(美因河)?靠双路径 + rating 解决,不显式判断语言。
- `"main street"` 是英语习语还是德语街名?同上。
- 用户可能混合语序:`"main station Berlin"`(法/意/西式语序)vs `"Berlin main station"`(英式)。Jung et al. 的 single-field search 枚举切分,天然兼容。

### 不处理的情况

- 复合词内部的类别词拆分:`"Kölner Dom"` 中的 `"Kölner"` 是 Köln 的形容词形式,需要 -er 词尾处理。当前只在 exonym 表里手工覆盖高频组合。
- 完整的英语句式:`"the cathedral in Cologne please"`。当前依赖 IDF/ILT 过滤 filler token,不做 NLP 解析。
- 多语言:法语、意大利语等其他源语言。设计上可扩展(同义词表结构支持),但当前只做英语。

---

## Evaluation 计划

准备两个测试套件,在 baseline 和加了 feature 的版本上分别跑,报告召回率。

### Feature 1 测试(约 20 条)

```
Düsseldorf / Duesseldorf / Dusseldorf / DUSSELDORF
Straße / Strasse / Str. / str
Aßlar / Asslar / Aslar / ABlar
Nürnberger Str. / Nuernberger Strasse / Nurnberger str
Müllerstraße / Muellerstrasse / Mullerstrasse / Mueller Strasse
Würzburg / Wuerzburg / Wurzburg
Tübingen / Tuebingen / Tubingen
Marienplatz / Marienpl.
```

### Feature 2 测试(约 20 条)

```
Munich main station → München Hauptbahnhof
Cologne cathedral → Kölner Dom
Berlin airport → Flughafen Berlin
Bavaria → Bayern
main station Stuttgart → Stuttgart Hauptbahnhof (词序变化)
Stuttgart HBF → Stuttgart Hauptbahnhof (缩写)
Nuremberg castle → Nürnberger Burg
Munich central station → München Hauptbahnhof
train station Aalen → Aalen Bahnhof
Hanover town hall → Rathaus Hannover
Cologne bridge → Kölner Brücke (若存在)
Frankfurt main station → 双候选(Frankfurt am Main + Frankfurt Hbf)
```

在 presentation 上展示对比表:baseline 全失败,加了 feature 后大部分成功。这是最直观的评分证据。

---

## 工作量估计

| 模块 | 大致代码量 |
|---|---|
| Feature 1 规范化函数 + 后缀拆分 | ~80 行 |
| 双索引集成 | ~30 行(改现有索引构建) |
| Feature 2a Exonym 表 + PBF `name:en` 抽取 | ~50 行 + 数据 |
| Feature 2b Synonym 表 | ~30 行 + 数据 |
| 双路径查询 + 结果合并 | ~50 行 |
| POI 数据纳入索引(`railway=station`, `amenity=*` 等) | ~40 行(改 PBF 解析) |
| 测试套件 | ~100 行 |
| **合计** | **~380 行核心代码 + 数据表** |

对一个学期的 Fachpraktikum,工作量适中,每一行都能在 presentation 上讲出用户故事。

---

## 参考文献

- Jung, C., Karch, D., Knopp, S., Luxen, D., & Sanders, P. (2011). *Efficient Error-Correcting Geocoding*. arXiv:1102.3306. — 主要 baseline pipeline
- Damerau, F. J. (1964). *A technique for computer detection and correction of spelling errors*. CACM 7(3). — Fuzzy search 理论基础
- Kukich, K. (1992). *Techniques for automatically correcting words in text*. ACM Computing Surveys 24(4). — 综述
- DIN 5007-2 — 德语 umlaut 官方替写规则
- OpenStreetMap Nominatim `Special_Phrases` — 多语言 POI 短语表(参考)
- Wikidata `wdt:P1448` (official name), `rdfs:label@en` — Exonym 数据来源(可选)
