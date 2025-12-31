# GitHub 分支保护规则配置指南

## 目的

确保所有合并到主分支的代码都经过自动化测试和代码审查，保证代码质量。

## 配置步骤

### 1. 进入仓库设置

1. 打开 GitHub 仓库页面
2. 点击 **Settings** (设置)
3. 左侧菜单选择 **Branches** (分支)
4. 点击 **Add branch protection rule** (添加分支保护规则)

### 2. 配置保护规则

#### 基本设置

**Branch name pattern (分支名称模式):**
```
main
```

或者同时保护多个分支：
```
main
develop
```

#### 必需的保护选项

勾选以下选项：

##### ✅ Require a pull request before merging
- **要求通过 Pull Request 才能合并**
- 子选项：
  - ✅ Require approvals: **1** (至少需要 1 个审批)
  - ✅ Dismiss stale pull request approvals when new commits are pushed
    (新提交时取消旧的审批)
  - ✅ Require review from Code Owners (如果有 CODEOWNERS 文件)

##### ✅ Require status checks to pass before merging
- **要求状态检查通过才能合并**
- ✅ Require branches to be up to date before merging
  (合并前要求分支是最新的)
- **必需的状态检查 (Required status checks):**
  - `build-linux (gcc, Debug)`
  - `build-linux (gcc, Release)`
  - `build-linux (clang, Debug)`
  - `build-linux (clang, Release)`
  - `build-windows (Debug)`
  - `build-windows (Release)`
  - `build-macos (Debug)`
  - `build-macos (Release)`
  - `format-check`
  - `static-analysis`

##### ✅ Require conversation resolution before merging
- **要求解决所有对话才能合并**

##### ✅ Require signed commits (可选)
- **要求签名提交**

##### ✅ Require linear history (推荐)
- **要求线性历史**
- 禁止 merge commits，只允许 rebase 或 squash

##### ✅ Include administrators
- **规则也适用于管理员**

### 3. 可选的高级设置

#### 🔒 Restrict who can push to matching branches
- 限制谁可以推送到匹配的分支
- 只允许特定团队或用户推送

#### 🔒 Allow force pushes
- ❌ 不要勾选（禁止强制推送）

#### 🔒 Allow deletions
- ❌ 不要勾选（禁止删除分支）

### 4. 保存规则

点击 **Create** 或 **Save changes** 保存配置。

## 工作流程

### 开发者工作流

1. **创建功能分支**
   ```bash
   git checkout -b feature/my-feature
   ```

2. **开发和提交**
   ```bash
   git add .
   git commit -m "feat: add new feature"
   git push origin feature/my-feature
   ```

3. **创建 Pull Request**
   - 在 GitHub 上创建 PR
   - 填写 PR 描述，说明改动内容
   - 等待 CI 检查通过

4. **CI 自动检查**
   - ✅ Linux (GCC/Clang) 编译和测试
   - ✅ Windows (MSVC) 编译和测试
   - ✅ macOS 编译和测试
   - ✅ 代码格式检查
   - ✅ 静态代码分析

5. **代码审查**
   - 至少 1 个审批者批准
   - 解决所有评论和建议

6. **合并**
   - 所有检查通过 ✅
   - 获得审批 ✅
   - 点击 **Merge pull request**

### PR 被阻止的常见原因

❌ **CI 检查失败**
- 编译错误
- 测试失败
- 代码格式不符合规范
- 静态分析发现问题

**解决方法：**
```bash
# 本地运行测试
cmake -B build -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure

# 检查代码格式
clang-format -i include/**/*.h src/**/*.cpp tests/**/*.cpp

# 运行静态分析
clang-tidy -p build include/**/*.cpp src/**/*.cpp
```

❌ **缺少审批**
- 需要至少 1 个团队成员审批

❌ **有未解决的对话**
- 回复所有评论
- 点击 "Resolve conversation"

❌ **分支不是最新的**
- 需要先合并或 rebase 主分支

**解决方法：**
```bash
# 方法1: Rebase (推荐)
git fetch origin
git rebase origin/main
git push --force-with-lease

# 方法2: Merge
git fetch origin
git merge origin/main
git push
```

## 状态徽章

在 README.md 中添加 CI 状态徽章：

```markdown
[![CI](https://github.com/YOUR_USERNAME/MagnetDownload/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/MagnetDownload/actions/workflows/ci.yml)
```

## 本地预检查脚本

创建 `.git/hooks/pre-push` 脚本，在推送前自动检查：

```bash
#!/bin/bash
set -e

echo "Running pre-push checks..."

# 代码格式检查
echo "Checking code format..."
find include src tests -name '*.h' -o -name '*.cpp' | \
xargs clang-format --dry-run --Werror

# 编译测试
echo "Building and testing..."
cmake -B build -DBUILD_TESTS=ON -DBUILD_MAIN_PROJECT=OFF
cmake --build build
cd build && ctest --output-on-failure

echo "All checks passed! ✅"
```

赋予执行权限：
```bash
chmod +x .git/hooks/pre-push
```

## 团队协作建议

1. **小而频繁的 PR**
   - 每个 PR 专注于一个功能或修复
   - 避免大型 PR（超过 500 行改动）

2. **清晰的提交信息**
   - 使用约定式提交 (Conventional Commits)
   - `feat:`, `fix:`, `docs:`, `test:`, `refactor:`

3. **及时的代码审查**
   - 24 小时内响应 PR
   - 提供建设性的反馈

4. **保持分支更新**
   - 定期 rebase 主分支
   - 避免长期存在的功能分支

## 故障排除

### CI 在 GitHub 上失败，但本地通过

1. 检查 GitHub Actions 日志
2. 确保本地环境与 CI 环境一致
3. 检查是否有平台特定的问题

### 无法推送到保护分支

```
remote: error: GH006: Protected branch update failed
```

**原因：** 直接推送到保护分支被禁止

**解决：** 创建功能分支并提交 PR

### 状态检查一直等待

**原因：** CI workflow 可能没有触发

**解决：**
1. 检查 `.github/workflows/ci.yml` 是否存在
2. 检查 workflow 的触发条件
3. 在 Actions 页面手动触发

## 参考资料

- [GitHub Branch Protection Rules](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Conventional Commits](https://www.conventionalcommits.org/)
