# Custom Agent 试验配置

这组配置是给 `kiro-cli` 的 custom agent 试验用的，目标是把 agent 名字同步到键盘板子。

当前提供 4 个示例 agent：

- `planner`
- `coder`
- `reviewer`
- `runner`

每个 agent 都使用同一个 hook 脚本，只是 `--agent-name` 不同。

如果你要改提示词，编辑对应的 `prompts/*.md` 文件。
