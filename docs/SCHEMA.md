# Schema

当前数据库核心表：

- `clips`
  - 条目主表
- `clip_formats`
  - 原始格式扩展表
- `groups`
  - 单层分组
- `settings`
  - 键值设置
- `clips_fts`
  - 搜索索引

`clips` 关键字段：

- `id`
- `title`
- `plain_text`
- `html_text`
- `content_type`
- `storage_path`
- `source_app_name`
- `source_app_path`
- `source_window_title`
- `is_pinned`
- `is_favorite`
- `group_id`
- `image_width`
- `image_height`
- `content_hash`
- `created_at`
- `updated_at`

命令协议：

- `CommandRequest`
  - `id`
  - `clip_id`
  - `query`
  - `action_id`
  - `wants_json`
- `CommandResponse`
  - `ok`
  - `code`
  - `message`
  - `data`
