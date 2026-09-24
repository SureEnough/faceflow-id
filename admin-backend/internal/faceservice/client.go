// Package faceservice 人脸识别特征服务客户端。
//
// 对应 face-service/（Python FastAPI）：POST /api/face/extract
// 上传头像图片，返回与边缘盒同向量空间的 512 维 float32 特征（base64 小端）。
package faceservice

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/url"
	"strings"
	"time"
)

// DefaultBaseURL face-service 默认地址（未配置时使用；
// 环境变量 FACE_SERVICE_URL 可覆盖默认值）。
const DefaultBaseURL = "http://127.0.0.1:8090"

// DefaultTimeout 单次提取请求超时。
const DefaultTimeout = 15 * time.Second

var (
	// ErrNoFace 图片中未检测到人脸（或图片解码失败）。
	ErrNoFace = errors.New("no face detected")
	// ErrServiceUnavailable face-service 不可达 / 返回异常。
	ErrServiceUnavailable = errors.New("face-service unavailable")
)

// Config 客户端配置。
// APIKey 兼容两种鉴权：
//   - 普通密钥：请求头 X-API-Key（face-service 方式）；
//   - 含 ":" 的 user:pass：HTTP Basic Auth（边缘盒 Web 界面方式，如 "admin:密码"）。
type Config struct {
	BaseURL string // 接口地址，如 http://127.0.0.1:8090 或 http://<edge-box>:8180
	APIKey  string // 密钥；空则不携带鉴权头
}

// Client face-service HTTP 客户端。
type Client struct {
	baseURL string
	apiKey  string
	http    *http.Client
}

// New 创建客户端。
func New(cfg Config) *Client {
	base := strings.TrimRight(cfg.BaseURL, "/")
	if base == "" {
		base = DefaultBaseURL
	}
	return &Client{
		baseURL: base,
		apiKey:  cfg.APIKey,
		http:    &http.Client{Timeout: DefaultTimeout},
	}
}

// authorize 按密钥格式设置鉴权头：含 ":" -> Basic Auth；否则 X-API-Key。
func (c *Client) authorize(req *http.Request) {
	if c.apiKey == "" {
		return
	}
	user, pass, ok := strings.Cut(c.apiKey, ":")
	if ok {
		req.SetBasicAuth(user, pass)
	} else {
		req.Header.Set("X-API-Key", c.apiKey)
	}
}

// extractResp face-service 统一响应。
type extractResp struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    struct {
		FeatureB64 string `json:"feature_b64"`
		Dim        int    `json:"dim"`
		Faces      int    `json:"faces"`
		Engine     string `json:"engine"`
	} `json:"data"`
}

// Extract 提取人脸特征。
// image 为原始图片字节（png/jpg/bmp），返回 512*4 字节 little-endian float32 特征。
func (c *Client) Extract(ctx context.Context, image []byte, mime string) ([]byte, error) {
	if len(image) == 0 {
		return nil, errors.New("empty image")
	}
	endpoint := c.baseURL + "/api/face/extract"

	var buf bytes.Buffer
	w := multipart.NewWriter(&buf)
	fw, err := w.CreateFormFile("image", "face.jpg")
	if err != nil {
		return nil, err
	}
	if _, err := fw.Write(image); err != nil {
		return nil, err
	}
	_ = w.Close()

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint, &buf)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", w.FormDataContentType())
	c.authorize(req)

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrServiceUnavailable, err)
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(io.LimitReader(resp.Body, 8<<20))
	if err != nil {
		return nil, fmt.Errorf("%w: read body: %v", ErrServiceUnavailable, err)
	}
	if resp.StatusCode == http.StatusUnauthorized {
		return nil, fmt.Errorf("face-service: %w: api key rejected (401)", ErrServiceUnavailable)
	}
	if resp.StatusCode == http.StatusNotFound {
		return nil, ErrNoFace
	}
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("%w: http %d: %s", ErrServiceUnavailable, resp.StatusCode, truncate(string(body), 200))
	}

	var out extractResp
	if err := json.Unmarshal(body, &out); err != nil {
		return nil, fmt.Errorf("%w: bad response: %v", ErrServiceUnavailable, err)
	}
	if out.Code != 0 {
		if out.Code == 404 || strings.Contains(out.Message, "no face") {
			return nil, ErrNoFace
		}
		return nil, fmt.Errorf("%w: code=%d msg=%s", ErrServiceUnavailable, out.Code, truncate(out.Message, 200))
	}
	feat, err := base64.StdEncoding.DecodeString(out.Data.FeatureB64)
	if err != nil {
		return nil, fmt.Errorf("%w: bad feature b64: %v", ErrServiceUnavailable, err)
	}
	if out.Data.Dim > 0 && len(feat) != out.Data.Dim*4 {
		return nil, fmt.Errorf("%w: feature length %d != dim*4 %d", ErrServiceUnavailable, len(feat), out.Data.Dim*4)
	}
	return feat, nil
}

// Ping 健康检查（可选）。
func (c *Client) Ping(ctx context.Context) error {
	u, err := url.Parse(c.baseURL + "/api/health")
	if err != nil {
		return err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u.String(), nil)
	if err != nil {
		return err
	}
	resp, err := c.http.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("health http %d", resp.StatusCode)
	}
	return nil
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "..."
}