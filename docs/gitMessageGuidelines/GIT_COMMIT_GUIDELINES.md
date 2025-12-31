# Git Commit Message 规范

## 概述

本项目采用 [Conventional Commits](https://www.conventionalcommits.org/) 规范，确保提交历史清晰、可追溯，便于自动化工具生成 CHANGELOG。

## 提交格式

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

### 基本规则

1. **标题行（必需）**：`<type>(<scope>): <subject>`
   - 不超过 50 字符
   - 使用祈使句（如 "add" 而非 "added"）
   - 首字母小写
   - 结尾不加句号

2. **正文（可选）**：详细描述变更内容
   - 与标题空一行
   - 每行不超过 72 字符
   - 说明"是什么"和"为什么"，而非"怎么做"

3. **页脚（可选）**：关联 Issue 或标注破坏性变更
   - `Closes #123` 或 `Fixes #456`
   - `BREAKING CHANGE: 描述`

## Type 类型

| Type | 说明 | 示例 |
|------|------|------|
| `feat` | 新功能 | 实现 DHT 协议、添加下载队列 |
| `fix` | Bug 修复 | 修复内存泄漏、解决连接超时 |
| `docs` | 文档变更 | 更新 README、添加 API 文档 |
| `style` | 代码格式 | 格式化代码、调整缩进 |
| `refactor` | 重构 | 重构网络模块、优化类结构 |
| `perf` | 性能优化 | 优化解析速度、减少内存占用 |
| `test` | 测试相关 | 添加单元测试、修复测试用例 |
| `build` | 构建系统 | 更新 CMakeLists.txt、修改依赖 |
| `ci` | CI/CD | 更新 GitHub Actions、添加自动化测试 |
| `chore` | 其他杂项 | 更新 .gitignore、清理临时文件 |
| `revert` | 回滚提交 | 回滚之前的某个提交 |

## Scope 范围

根据项目模块划分：

### 核心模块
- `bencode`: Bencode 编解码器
- `torrent`: Torrent 文件解析
- `dht`: DHT 协议实现
- `tracker`: Tracker 通信
- `peer`: Peer 连接和通信
- `download`: 下载管理器

### 网络模块
- `network`: 通用网络功能
- `udp`: UDP 客户端/服务器
- `tcp`: TCP 客户端/服务器
- `http`: HTTP 客户端

### 工具模块
- `utils`: 工具函数
- `crypto`: 加密相关
- `storage`: 文件存储

### 其他
- `tests`: 测试相关
- `docs`: 文档
- `build`: 构建配置
- `deps`: 第三方依赖

## 提交示例

### 基础示例

```bash
# 新功能
feat(bencode): implement bencode parser

# Bug 修复
fix(udp): handle connection timeout correctly

# 文档更新
docs(readme): add build instructions for Linux

# 测试
test(bencode): add unit tests for list parsing

# 重构
refactor(network): extract common socket logic

# 性能优化
perf(parser): optimize large file parsing performance
```

### 完整示例

```bash
feat(dht): implement DHT node discovery

Add DHT protocol support for peer discovery without tracker.
Implements BEP 5 specification with routing table management.

Features:
- Bootstrap from known nodes
- Routing table with K-buckets
- get_peers and announce_peer queries

Closes #45
```

### 破坏性变更示例

```bash
refactor(api)!: change download API signature

BREAKING CHANGE: Download::start() now returns Result<DownloadId>
instead of bool. Update all callers to handle the new return type.

Migration guide:
- Old: if (download.start()) { ... }
- New: if (auto id = download.start()) { ... }

Closes #78
```

### 多个 Issue 关联

```bash
fix(peer): resolve multiple connection issues

- Fix memory leak in peer connection pool
- Handle unexpected disconnection gracefully
- Add connection retry mechanism

Fixes #123, #124, #125
```

## 特殊情况

### 回滚提交

```bash
revert: feat(dht): implement DHT node discovery

This reverts commit a1b2c3d4.
Reason: DHT implementation causes memory leak in production.
```

### 合并提交

```bash
Merge branch 'feature/dht-support' into main

Implements DHT protocol support for decentralized peer discovery.
```

## 工具配置

### 使用 Commitizen（推荐）

```bash
# 安装
npm install -g commitizen cz-conventional-changelog

# 初始化
commitizen init cz-conventional-changelog --save-dev --save-exact

# 使用（代替 git commit）
git cz
```

### 使用 Commitlint（检查规范）

```bash
# 安装
npm install --save-dev @commitlint/cli @commitlint/config-conventional

# 配置 .commitlintrc.json
{
  "extends": ["@commitlint/config-conventional"]
}

# 配合 husky 使用
npx husky add .husky/commit-msg 'npx --no -- commitlint --edit "$1"'
```

## 最佳实践

### ✅ 推荐做法

```bash
feat(bencode): add support for nested dictionaries
fix(udp): prevent buffer overflow in receive handler
docs(api): document Download class public methods
test(torrent): add integration tests for file parsing
refactor(network): simplify error handling logic
```

### ❌ 不推荐做法

```bash
# 太模糊
fix: bug fix

# 太长
feat(bencode): implement a comprehensive bencode parser with support for all data types including integers, strings, lists and dictionaries

# 不规范
Fixed bug in network module.
added new feature
Update README.md
```

## 提交频率建议

- **小步提交**：每个提交只做一件事
- **功能完整**：确保每次提交代码可编译、可运行
- **及时提交**：完成一个小功能就提交，不要积累太多变更
- **原子性**：一个提交应该是一个完整的逻辑单元

## 检查清单

提交前请确认：

- [ ] 提交信息符合 `<type>(<scope>): <subject>` 格式
- [ ] Type 和 Scope 使用正确
- [ ] 标题行不超过 50 字符
- [ ] 代码已通过编译和测试
- [ ] 已关联相关 Issue（如有）
- [ ] 破坏性变更已标注 `BREAKING CHANGE`

## 参考资源

- [Conventional Commits 规范](https://www.conventionalcommits.org/)
- [Angular Commit Guidelines](https://github.com/angular/angular/blob/main/CONTRIBUTING.md#commit)
- [Semantic Versioning](https://semver.org/)

---

**注意**：遵循规范不仅能让项目历史更清晰，还能自动化生成 CHANGELOG 和版本号管理。
