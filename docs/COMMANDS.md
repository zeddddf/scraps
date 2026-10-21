# Commands

当前首版保留的命令边界：

- `show`
  - 唤起主窗口；如果主进程未运行，会先启动 GUI
- `paste-latest`
  - 粘贴最近一条
- `paste --id <id>`
  - 粘贴指定条目
- `transform-paste --id <id> --action <action>`
  - 变形后粘贴
- `search <keyword> [--json]`
  - 搜索历史

支持的变形动作：

- `upper`
- `lower`
- `camel`
- `trim`
- `url-encode`
- `url-decode`
- `json-pretty`

`search --json` 返回：

- `ok`
- `code`
- `message`
- `data`

其中 `data` 是数组字符串，元素字段为：

- `id`
- `title`
- `snippet`
- `contentType`
- `isPinned`
- `isFavorite`
- `groupId`
- `sourceApp`
- `updatedAt`
