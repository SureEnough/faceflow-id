package api_test

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"

	"admin-backend/internal/api"
	"admin-backend/internal/config"
	"admin-backend/internal/security"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

type respEnvelope struct {
	Code    int             `json:"code"`
	Message string          `json:"message"`
	Data    json.RawMessage `json:"data"`
}

func newTestServer(t *testing.T) (*httptest.Server, func()) {
	t.Helper()
	gin.SetMode(gin.TestMode)
	cfg := config.Load()
	cfg.DBDSN = "sqlite::memory:"
	cfg.DevicePSK = "test-psk"
	cfg.JWTSecret = "test-secret"
	cfg.AdminPassword = "admin123"

	cip, err := security.NewCipher("") // 测试不加密
	if err != nil {
		t.Fatalf("cipher: %v", err)
	}
	db, err := storage.Open(cfg.DBDSN)
	if err != nil {
		t.Fatalf("db: %v", err)
	}
	if err := storage.AutoMigrate(db); err != nil {
		t.Fatalf("migrate: %v", err)
	}
	srv := api.NewServer(cfg, db, cip)
	if err := srv.EnsureAdmin(); err != nil {
		t.Fatalf("ensure admin: %v", err)
	}
	ts := httptest.NewServer(srv.Router())
	return ts, ts.Close
}

func doJSON(t *testing.T, method, url string, body any, token string) (respEnvelope, int) {
	t.Helper()
	var buf bytes.Buffer
	if body != nil {
		if err := json.NewEncoder(&buf).Encode(body); err != nil {
			t.Fatalf("encode: %v", err)
		}
	}
	req, err := http.NewRequest(method, url, &buf)
	if err != nil {
		t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("do: %v", err)
	}
	defer resp.Body.Close()
	var out respEnvelope
	_ = json.NewDecoder(resp.Body).Decode(&out)
	return out, resp.StatusCode
}

func login(t *testing.T, base, user, pass string) string {
	t.Helper()
	out, status := doJSON(t, http.MethodPost, base+"/auth/login", map[string]string{"username": user, "password": pass}, "")
	if status != 200 || out.Code != 0 {
		t.Fatalf("login failed: status=%d code=%d msg=%s", status, out.Code, out.Message)
	}
	var d struct{ Token string `json:"token"` }
	if err := json.Unmarshal(out.Data, &d); err != nil || d.Token == "" {
		t.Fatalf("login token missing: %v", err)
	}
	return d.Token
}

func featureBase64() string {
	f := make([]byte, 512*4) // 2048B = 512×float32
	for i := range f {
		f[i] = byte(i*31 + 7)
	}
	return base64.StdEncoding.EncodeToString(f)
}

func TestFullFlowLoginDeviceCustomerHistory(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	// 1. 登录
	token := login(t, base, "admin", "admin123")

	// 2. 设备注册（主设备 + 子设备）
	out, status := doJSON(t, http.MethodPost, base+"/devices/register", map[string]any{
		"device_type": 1, "name": "边缘盒-T", "store_id": 1, "psk": "test-psk",
	}, "")
	if status != 200 || out.Code != 0 {
		t.Fatalf("register edge: %d %s", out.Code, out.Message)
	}
	var edge struct{ DeviceID uint64 `json:"device_id"` }
	_ = json.Unmarshal(out.Data, &edge)

	// 3. 非法父设备（读卡器挂到边缘盒下 → 类型不匹配应拒绝）
	out, status = doJSON(t, http.MethodPost, base+"/devices/register", map[string]any{
		"device_type": 5, "parent_id": edge.DeviceID, "device_key": "reader-x",
		"name": "非法读卡器", "store_id": 1, "psk": "test-psk",
	}, "")
	if status == 200 && out.Code == 0 {
		t.Fatal("invalid parent type should be rejected")
	}

	// 4. 批量上报 2 条匿名轨迹
	feat := featureBase64()
	recs := []map[string]any{
		{"track_id": "T-1", "person_type": 0, "face_feature": feat, "direction": 0, "camera_id": "cam-01", "created_at": "2026-09-18T10:00:00Z"},
		{"track_id": "T-2", "person_type": 0, "face_feature": feat, "direction": 0, "camera_id": "cam-01", "created_at": "2026-09-20T10:00:00Z"},
	}
	out, status = doJSON(t, http.MethodPost, base+"/records/recognition/batch", map[string]any{"device_id": edge.DeviceID, "records": recs}, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("batch: %d %s", out.Code, out.Message)
	}
	// 幂等：重复上报同一批应 skipped
	out, _ = doJSON(t, http.MethodPost, base+"/records/recognition/batch", map[string]any{"device_id": edge.DeviceID, "records": recs}, token)
	var idem struct{ Accepted, Skipped int }
	_ = json.Unmarshal(out.Data, &idem)
	if idem.Accepted != 0 || idem.Skipped != 2 {
		t.Fatalf("idempotency failed: accepted=%d skipped=%d", idem.Accepted, idem.Skipped)
	}

	// 5. 顾客录入 → 历史回查 2 次/2 天
	out, status = doJSON(t, http.MethodPost, base+"/customers", map[string]any{
		"person_type": 0, "name": "张三", "id_card_no": "110101199001011234", "face_feature": feat,
	}, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("create customer: %d %s", out.Code, out.Message)
	}
	var cust struct {
		History struct {
			TotalVisits int64 `json:"total_visits"`
			VisitDays   int64 `json:"visit_days"`
		} `json:"history"`
	}
	_ = json.Unmarshal(out.Data, &cust)
	if cust.History.TotalVisits != 2 || cust.History.VisitDays != 2 {
		t.Fatalf("history mismatch: %+v", cust.History)
	}

	// 6. 客流统计（顾客）
	out, status = doJSON(t, http.MethodGet, fmt.Sprintf("%s/stats/flow?granularity=day&start_at=%s", base, "2026-09-01T00:00:00Z"), nil, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("stats flow: %d %s", out.Code, out.Message)
	}
}

func TestRBACForbiddenForViewer(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	adminToken := login(t, base, "admin", "admin123")
	// 创建 viewer
	_, status := doJSON(t, http.MethodPost, base+"/users", map[string]any{"username": "v1", "password": "viewer123", "role": 2, "status": 1}, adminToken)
	if status != 200 {
		t.Fatalf("create viewer: %d", status)
	}
	viewerToken := login(t, base, "v1", "viewer123")

	// viewer 读允许
	out, status := doJSON(t, http.MethodGet, base+"/customers", nil, viewerToken)
	if status != 200 || out.Code != 0 {
		t.Fatalf("viewer read should pass: %d %s", out.Code, out.Message)
	}
	// viewer 写拒绝
	_, status = doJSON(t, http.MethodPost, base+"/customers", map[string]any{
		"person_type": 1, "name": "x", "staff_no": "S1", "face_feature": featureBase64(),
	}, viewerToken)
	if status != http.StatusForbidden {
		t.Fatalf("viewer write should be 403, got %d", status)
	}
	// viewer 用户管理拒绝
	_, status = doJSON(t, http.MethodGet, base+"/users", nil, viewerToken)
	if status != http.StatusForbidden {
		t.Fatalf("viewer users should be 403, got %d", status)
	}
}