package faceservice

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

func testFeature() string {
	feat := make([]byte, 512*4)
	for i := range feat {
		feat[i] = byte(i)
	}
	return base64.StdEncoding.EncodeToString(feat)
}

func TestExtractOK(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/face/extract" {
			t.Errorf("path = %s", r.URL.Path)
		}
		if r.Header.Get("X-API-Key") != "secret" {
			t.Errorf("api key = %q", r.Header.Get("X-API-Key"))
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"code": 0, "message": "ok", "data": map[string]any{
				"feature_b64": testFeature(), "dim": 512, "faces": 1, "engine": "mock",
			},
		})
	}))
	defer srv.Close()

	c := New(Config{BaseURL: srv.URL, APIKey: "secret"})
	feat, err := c.Extract(context.Background(), []byte("image-bytes"), "image/jpeg")
	if err != nil {
		t.Fatalf("Extract: %v", err)
	}
	if len(feat) != 512*4 {
		t.Fatalf("feature len = %d", len(feat))
	}
}

func TestExtractNoFace(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNotFound)
		_, _ = w.Write([]byte(`{"code":404,"message":"no face detected"}`))
	}))
	defer srv.Close()

	c := New(Config{BaseURL: srv.URL})
	_, err := c.Extract(context.Background(), []byte("x"), "image/jpeg")
	if err == nil {
		t.Fatal("expected error")
	}
	if err != ErrNoFace {
		t.Fatalf("err = %v, want ErrNoFace", err)
	}
}

func TestExtractAuthError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		_, _ = w.Write([]byte(`{"code":401,"message":"invalid api key"}`))
	}))
	defer srv.Close()

	c := New(Config{BaseURL: srv.URL, APIKey: "wrong"})
	_, err := c.Extract(context.Background(), []byte("x"), "image/jpeg")
	if err == nil {
		t.Fatal("expected error")
	}
	if err == ErrNoFace {
		t.Fatalf("err should not be ErrNoFace, got %v", err)
	}
}

func TestExtractUnreachable(t *testing.T) {
	c := New(Config{BaseURL: "http://127.0.0.1:1"})
	_, err := c.Extract(context.Background(), []byte("x"), "image/jpeg")
	if err == nil {
		t.Fatal("expected error")
	}
}

func TestNewDefaultBaseURL(t *testing.T) {
	c := New(Config{})
	if c.baseURL != DefaultBaseURL {
		t.Fatalf("baseURL = %q", c.baseURL)
	}
}

func TestEmptyImage(t *testing.T) {
	c := New(Config{})
	if _, err := c.Extract(context.Background(), nil, "image/jpeg"); err == nil {
		t.Fatal("expected error for empty image")
	}
}

func TestExtractBasicAuth(t *testing.T) {
	// 密钥含 ":" 时走 HTTP Basic Auth（边缘盒方式）
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		user, pass, ok := r.BasicAuth()
		if !ok || user != "admin" || pass != "admin123" {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"code": 0, "message": "ok", "data": map[string]any{
				"feature_b64": testFeature(), "dim": 512, "faces": 1, "engine": "edge-box",
			},
		})
	}))
	defer srv.Close()

	c := New(Config{BaseURL: srv.URL, APIKey: "admin:admin123"})
	feat, err := c.Extract(context.Background(), []byte("img"), "image/jpeg")
	if err != nil {
		t.Fatalf("Extract: %v", err)
	}
	if len(feat) != 512*4 {
		t.Fatalf("feature len = %d", len(feat))
	}
}