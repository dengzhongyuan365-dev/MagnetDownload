#!/bin/bash
# MagnetDownload Release Script
# 自动化版本发布脚本

set -e

# 默认参数
VERSION=""
PUSH_TAG=false
CREATE_PACKAGES=true
DRY_RUN=false

# 帮助信息
show_help() {
    cat << EOF
MagnetDownload Release Script

Usage: $0 [OPTIONS] VERSION

Arguments:
    VERSION                 Version to release (e.g., 1.0.1, 2.0.0-beta)

Options:
    --push                  Push tag to remote repository
    --no-packages           Don't create packages
    --dry-run               Show what would be done without executing
    -h, --help              Show this help message

Examples:
    $0 1.0.1                # Create local tag v1.0.1
    $0 1.0.1 --push         # Create and push tag v1.0.1
    $0 2.0.0-beta --push    # Create and push pre-release tag
EOF
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --push)
            PUSH_TAG=true
            shift
            ;;
        --no-packages)
            CREATE_PACKAGES=false
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        -*)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
        *)
            if [[ -z "$VERSION" ]]; then
                VERSION="$1"
            else
                echo "Error: Multiple versions specified"
                show_help
                exit 1
            fi
            shift
            ;;
    esac
done

# 验证版本参数
if [[ -z "$VERSION" ]]; then
    echo "Error: Version is required"
    show_help
    exit 1
fi

# 验证版本格式
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9-]+)?$ ]]; then
    echo "Error: Invalid version format '$VERSION'"
    echo "Expected format: MAJOR.MINOR.PATCH[-SUFFIX]"
    echo "Examples: 1.0.0, 1.2.3, 2.0.0-beta, 1.0.0-rc1"
    exit 1
fi

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TAG_NAME="v$VERSION"

echo "MagnetDownload Release Script"
echo "============================"
echo "Version: $VERSION"
echo "Tag: $TAG_NAME"
echo "Push Tag: $PUSH_TAG"
echo "Create Packages: $CREATE_PACKAGES"
echo "Dry Run: $DRY_RUN"
echo ""

cd "$PROJECT_ROOT"

# 检查Git状态
if [[ "$DRY_RUN" == false ]]; then
    if ! git diff --quiet; then
        echo "Error: Working directory has uncommitted changes"
        echo "Please commit or stash your changes before releasing"
        exit 1
    fi
    
    if ! git diff --cached --quiet; then
        echo "Error: Staging area has uncommitted changes"
        echo "Please commit your staged changes before releasing"
        exit 1
    fi
fi

# 检查标签是否已存在
if git tag -l | grep -q "^$TAG_NAME$"; then
    echo "Error: Tag '$TAG_NAME' already exists"
    echo "Existing tags:"
    git tag -l | grep "^v" | sort -V
    exit 1
fi

# 更新版本信息
echo "Updating version information..."

# 解析版本号
if [[ "$VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-(.+))?$ ]]; then
    MAJOR="${BASH_REMATCH[1]}"
    MINOR="${BASH_REMATCH[2]}"
    PATCH="${BASH_REMATCH[3]}"
    SUFFIX="${BASH_REMATCH[5]:-}"
else
    echo "Error: Failed to parse version '$VERSION'"
    exit 1
fi

# 更新CMakeLists.txt中的版本信息
if [[ "$DRY_RUN" == false ]]; then
    sed -i.bak \
        -e "s/set(MAGNETDOWNLOAD_VERSION_MAJOR [0-9]\+)/set(MAGNETDOWNLOAD_VERSION_MAJOR $MAJOR)/" \
        -e "s/set(MAGNETDOWNLOAD_VERSION_MINOR [0-9]\+)/set(MAGNETDOWNLOAD_VERSION_MINOR $MINOR)/" \
        -e "s/set(MAGNETDOWNLOAD_VERSION_PATCH [0-9]\+)/set(MAGNETDOWNLOAD_VERSION_PATCH $PATCH)/" \
        CMakeLists.txt
    
    if [[ -n "$SUFFIX" ]]; then
        sed -i.bak "s/set(MAGNETDOWNLOAD_VERSION_SUFFIX \"[^\"]*\")/set(MAGNETDOWNLOAD_VERSION_SUFFIX \"$SUFFIX\")/" CMakeLists.txt
    else
        sed -i.bak "s/set(MAGNETDOWNLOAD_VERSION_SUFFIX \"[^\"]*\")/set(MAGNETDOWNLOAD_VERSION_SUFFIX \"\")/" CMakeLists.txt
    fi
    
    rm CMakeLists.txt.bak
    
    # 提交版本更新
    git add CMakeLists.txt
    git commit -m "chore: bump version to $VERSION"
else
    echo "Would update CMakeLists.txt with version $VERSION"
fi

# 创建标签
echo "Creating tag '$TAG_NAME'..."
if [[ "$DRY_RUN" == false ]]; then
    git tag -a "$TAG_NAME" -m "Release version $VERSION

Features and changes in this release:
- [Add release notes here]

Build information:
- Version: $VERSION
- Tag: $TAG_NAME
- Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
"
else
    echo "Would create tag '$TAG_NAME'"
fi

# 推送标签
if [[ "$PUSH_TAG" == true ]]; then
    echo "Pushing tag to remote repository..."
    if [[ "$DRY_RUN" == false ]]; then
        git push origin main
        git push origin "$TAG_NAME"
    else
        echo "Would push tag '$TAG_NAME' to origin"
    fi
fi

# 创建包
if [[ "$CREATE_PACKAGES" == true ]]; then
    echo "Creating release packages..."
    if [[ "$DRY_RUN" == false ]]; then
        ./scripts/build.sh --clean -t Release
    else
        echo "Would create release packages"
    fi
fi

echo ""
echo "Release process completed!"
echo ""
echo "Next steps:"
if [[ "$PUSH_TAG" == false ]]; then
    echo "1. Push the tag: git push origin $TAG_NAME"
fi
echo "2. Create GitHub release at: https://github.com/yourusername/MagnetDownload/releases/new?tag=$TAG_NAME"
echo "3. Upload packages from build/ directory"
echo "4. Update release notes"

if [[ "$DRY_RUN" == true ]]; then
    echo ""
    echo "This was a dry run. No changes were made."
    echo "Run without --dry-run to execute the release."
fi