package object

import (
	"bytes"
	"context"
	"io"
	"os"
	"path/filepath"
	"testing"
)

func TestLocalPutGetDelete(t *testing.T) {
	dir := t.TempDir()
	s := NewLocal(dir, "https://static.example.com/")

	ctx := context.Background()
	key := "snapshots/2026/09/22/a.jpg"
	data := []byte("fake-jpeg-bytes")

	if err := s.Put(ctx, key, bytes.NewReader(data), "image/jpeg"); err != nil {
		t.Fatalf("put: %v", err)
	}

	rc, err := s.Get(ctx, key)
	if err != nil {
		t.Fatalf("get: %v", err)
	}
	got, err := io.ReadAll(rc)
	rc.Close()
	if err != nil || !bytes.Equal(got, data) {
		t.Fatalf("get content mismatch: %v", err)
	}

	if u := s.URL(ctx, key); u != "https://static.example.com/snapshots/2026/09/22/a.jpg" {
		t.Fatalf("url: %s", u)
	}

	if _, err := s.Get(ctx, "nope.jpg"); err != ErrNotFound {
		t.Fatalf("missing object should be ErrNotFound, got %v", err)
	}

	if err := s.Delete(ctx, key); err != nil {
		t.Fatalf("delete: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, filepath.FromSlash(key))); !os.IsNotExist(err) {
		t.Fatalf("file should be deleted")
	}
}

func TestLocalRejectTraversal(t *testing.T) {
	dir := t.TempDir()
	s := NewLocal(dir, "")
	if err := s.Put(context.Background(), "../escape.txt", bytes.NewReader(nil), ""); err == nil {
		t.Fatalf("path traversal must be rejected")
	}
}