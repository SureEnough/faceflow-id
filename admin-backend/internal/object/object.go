// admin-backend/internal/object/object.go
// 对象存储抽象（roadmap #5 生产部署）：抓拍图/证件照等二进制对象。
// 默认实现为本地磁盘（开发/单机）；生产可替换为 MinIO / S3 兼容存储
// （docs/deployment.md 提供 minio-go 接入示例）。
package object

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// ErrNotFound 对象不存在
var ErrNotFound = errors.New("object not found")

// Storage 对象存储接口
type Storage interface {
	// Put 写入对象；key 需为相对路径（如 snapshots/2026/09/22/xxx.jpg）
	Put(ctx context.Context, key string, r io.Reader, contentType string) error
	// Get 读取对象；不存在返回 ErrNotFound
	Get(ctx context.Context, key string) (io.ReadCloser, error)
	// Delete 删除对象
	Delete(ctx context.Context, key string) error
	// URL 返回可直接访问的 URL（对象存储 CDN / MinIO 公开桶 / 本地静态服务）
	URL(ctx context.Context, key string) string
	// Kind 返回实现类型（local/minio/s3），用于日志与运维
	Kind() string
}

// ---- 本地磁盘实现 ----

type locals struct {
	root      string
	publicURL string // 静态服务前缀（如 https://static.example.com/）
}

// NewLocal 创建本地磁盘存储：root 为数据目录，publicURL 为空表示不暴露公网地址
func NewLocal(root string, publicURL string) Storage {
	return &locals{root: root, publicURL: strings.TrimSuffix(publicURL, "/")}
}

func (s *locals) Kind() string { return "local" }

func (s *locals) path(key string) (string, error) {
	if key == "" || strings.Contains(key, "..") || strings.HasPrefix(key, "/") {
		return "", fmt.Errorf("invalid object key: %q", key)
	}
	return filepath.Join(s.root, filepath.FromSlash(key)), nil
}

func (s *locals) Put(ctx context.Context, key string, r io.Reader, _ string) error {
	p, err := s.path(key)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	f, err := os.Create(p)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = io.Copy(f, r)
	return err
}

func (s *locals) Get(ctx context.Context, key string) (io.ReadCloser, error) {
	p, err := s.path(key)
	if err != nil {
		return nil, err
	}
	f, err := os.Open(p)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, ErrNotFound
		}
		return nil, err
	}
	return f, nil
}

func (s *locals) Delete(ctx context.Context, key string) error {
	p, err := s.path(key)
	if err != nil {
		return err
	}
	if err := os.Remove(p); err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}

func (s *locals) URL(ctx context.Context, key string) string {
	if s.publicURL == "" {
		return ""
	}
	return s.publicURL + "/" + key
}