package api_test

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"admin-backend/internal/api"
	"admin-backend/internal/config"
	"admin-backend/internal/security"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// regEdge 注册一台边缘盒并返回 device_id + token
func regEdge(t *testing.T, base, name string, storeID uint64) (uint64, string) {
	t.Helper()
	out, status := doJSON(t, http.MethodPost, base+"/devices/register", map[string]any{
		"device_type": 1, "name": name, "store_id": storeID, "psk": "test-psk",
	}, "")
	if status != 200 || out.Code != 0 {
		t.Fatalf("register %s: %d %s", name, out.Code, out.Message)
	}
	var d struct {
		DeviceID uint64 `json:"device_id"`
		Token    string `json:"token"`
	}
	if err := json.Unmarshal(out.Data, &d); err != nil || d.DeviceID == 0 || d.Token == "" {
		t.Fatalf("register %s response invalid: %v", name, err)
	}
	return d.DeviceID, d.Token
}

// TestDeviceTokenCannotImpersonate 回归：设备 token 不得伪造其他设备数据
func TestDeviceTokenCannotImpersonate(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	edgeA, tokenA := regEdge(t, base, "边缘盒-A", 1)
	edgeB, _ := regEdge(t, base, "边缘盒-B", 1)
	adminToken := login(t, base, "admin", "admin123")

	// 1. 设备 A 不得以设备 B 身份上报识别记录 → 403
	rec := map[string]any{
		"device_id": edgeB,
		"records": []map[string]any{
			{"track_id": "T-X", "person_type": 0, "face_feature": featureBase64(),
				"direction": 0, "camera_id": "cam-1", "created_at": "2026-09-18T10:00:00Z"},
		},
	}
	if _, status := doJSON(t, http.MethodPost, base+"/records/recognition/batch", rec, tokenA); status != 403 {
		t.Fatalf("device A report as B should 403, got %d", status)
	}

	// 2. 设备 A 不得编辑（刷新在线状态）设备 B → 403
	if _, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeB),
		map[string]any{"status": 1}, tokenA); status != 403 {
		t.Fatalf("device A update B should 403, got %d", status)
	}

	// 3. 设备 A 不得读取设备 B 配置 → 403；读自己配置 → 200
	if _, status := doJSON(t, http.MethodGet, fmt.Sprintf("%s/devices/%d/config", base, edgeB), nil, tokenA); status != 403 {
		t.Fatalf("device A read B config should 403, got %d", status)
	}
	if _, status := doJSON(t, http.MethodGet, fmt.Sprintf("%s/devices/%d/config", base, edgeA), nil, tokenA); status != 200 {
		t.Fatalf("device A read own config should 200, got %d", status)
	}

	// 4. 用户 token（admin）不受限：可代为上报 / 编辑任意设备
	if _, status := doJSON(t, http.MethodPost, base+"/records/recognition/batch", rec, adminToken); status != 200 {
		t.Fatalf("admin report for B should 200, got %d", status)
	}
	if _, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeB),
		map[string]any{"status": 1}, adminToken); status != 200 {
		t.Fatalf("admin update B should 200, got %d", status)
	}
}

// newEncryptedTestServer 带真实 AES 密钥的测试服务（用于验证加密字段搜索）
func newEncryptedTestServer(t *testing.T) (*httptest.Server, func()) {
	t.Helper()
	gin.SetMode(gin.TestMode)
	cfg := config.Load()
	cfg.DBDSN = "sqlite::memory:"
	cfg.DevicePSK = "test-psk"
	cfg.JWTSecret = "test-secret"
	cfg.AdminPassword = "admin123"
	cfg.AESKey = strings.Repeat("ab", 32) // 64 hex chars = 32 bytes

	cip, err := security.NewCipher(cfg.AESKey)
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

// TestCustomerNameSearchEncrypted 回归：姓名 AES 加密后按姓名搜索不再 500，且过滤正确
func TestCustomerNameSearchEncrypted(t *testing.T) {
	ts, close := newEncryptedTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"
	token := login(t, base, "admin", "admin123")

	mk := func(personType int8, name, idCard, staffNo string) {
		body := map[string]any{"person_type": personType, "name": name, "face_feature": featureBase64()}
		if personType == 0 {
			body["id_card_no"] = idCard
		}
		if staffNo != "" {
			body["staff_no"] = staffNo
		}
		if _, status := doJSON(t, http.MethodPost, base+"/customers", body, token); status != 200 {
			t.Fatalf("create %s: %d", name, status)
		}
	}
	mk(0, "张三", "110101199001011234", "")
	mk(0, "李四", "110101199202022345", "")

	// 姓名模糊检索 → 仅命中张三
	out, status := doJSON(t, http.MethodGet, base+"/customers?name=%E5%BC%A0", nil, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("search by name should 200, got %d %s", out.Code, out.Message)
	}
	var list struct {
		Total int64                    `json:"total"`
		Items []map[string]interface{} `json:"items"`
	}
	if err := json.Unmarshal(out.Data, &list); err != nil {
		t.Fatal(err)
	}
	if list.Total != 1 || list.Items[0]["name"] != "张三" {
		t.Fatalf("name search mismatch: total=%d items=%+v", list.Total, list.Items)
	}

	// 无命中 → 200 且 total=0
	out, status = doJSON(t, http.MethodGet, base+"/customers?name=%E7%8E%8B", nil, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("no-hit search should 200, got %d %s", out.Code, out.Message)
	}
	if err := json.Unmarshal(out.Data, &list); err != nil {
		t.Fatal(err)
	}
	if list.Total != 0 {
		t.Fatalf("no-hit search total should 0, got %d", list.Total)
	}
}

// TestHistorySearchReturnsSnapshot 回归：历史回查匹配明细携带抓拍快照
func TestHistorySearchReturnsSnapshot(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	edgeID, _ := regEdge(t, base, "边缘盒-快照", 1)
	token := login(t, base, "admin", "admin123")
	feat := featureBase64()
	snap := base64.StdEncoding.EncodeToString([]byte("fake-jpeg-bytes"))

	// 上报一条带快照的识别记录（开发模式 obj=nil → 快照原样文本入库）
	_, status := doJSON(t, http.MethodPost, base+"/records/recognition/batch", map[string]any{
		"device_id": edgeID,
		"records": []map[string]any{
			{"track_id": "T-SNAP", "person_type": 0, "face_feature": feat, "direction": 0,
				"camera_id": "cam-1", "snapshot": snap, "snapshot_mime": "image/jpeg",
				"created_at": "2026-09-18T10:00:00Z"},
		},
	}, token)
	if status != 200 {
		t.Fatalf("batch: %d", status)
	}

	// 手动回查 → 匹配明细应带 snapshot
	out, status := doJSON(t, http.MethodPost, base+"/history/search", map[string]any{
		"face_feature": feat, "similarity_threshold": 0.01,
	}, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("history search: %d %s", out.Code, out.Message)
	}
	var res struct {
		Matched []map[string]interface{} `json:"matched_records"`
	}
	if err := json.Unmarshal(out.Data, &res); err != nil {
		t.Fatal(err)
	}
	if len(res.Matched) == 0 {
		t.Fatal("expected matched record")
	}
	if got, _ := res.Matched[0]["snapshot"].(string); got != snap {
		t.Fatalf("snapshot mismatch: got %q want %q", got, snap)
	}
	if got, _ := res.Matched[0]["snapshot_mime"].(string); got != "image/jpeg" {
		t.Fatalf("snapshot_mime mismatch: %q", got)
	}
}
// TestUpdateDeviceOnline 回归：设备 token 通过编辑设备接口刷新"最后在线时间"
func TestUpdateDeviceOnline(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"

	edgeID, token := regEdge(t, base, "边缘盒-在线", 1)
	camKey := "cam-01"
	if _, status := doJSON(t, http.MethodPost, base+"/devices/register", map[string]any{
		"device_type": 3, "parent_id": edgeID, "device_key": camKey,
		"name": camKey, "store_id": 1, "psk": "test-psk",
	}, ""); status != 200 {
		t.Fatalf("register cam: %d", status)
	}

	// 1. 设备 token 更新自己：刷新在线 + 子设备上线
	out, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"status": 1, "sub_devices": []map[string]any{
			{"device_key": camKey, "type": 3, "online": true},
		}}, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("device update self: %d %s", out.Code, out.Message)
	}
	var up struct {
		LastSeenAt int64 `json:"last_seen_at"`
		Status        int8  `json:"status"`
	}
	if err := json.Unmarshal(out.Data, &up); err != nil {
		t.Fatal(err)
	}
	if up.LastSeenAt == 0 || up.Status != 1 {
		t.Fatalf("device online not refreshed: %+v", up)
	}

	// 2. 设备树中主设备和子设备均在线
	out, status = doJSON(t, http.MethodGet, base+"/devices", nil, token)
	if status != 200 {
		t.Fatalf("device tree: %d", status)
	}
	var tree struct {
		Items []map[string]interface{} `json:"items"`
	}
	if err := json.Unmarshal(out.Data, &tree); err != nil {
		t.Fatal(err)
	}
	if len(tree.Items) != 1 || tree.Items[0]["status"].(float64) != 1 {
		t.Fatalf("edge not online: %+v", tree.Items)
	}

	// 3. 设备 token 尝试改 name → 应被忽略（仍返回 200，但名称不变）
	_, status = doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"name": "被篡改名"}, token)
	if status != 200 {
		t.Fatalf("device name attempt should 200(ignored), got %d", status)
	}
	out, _ = doJSON(t, http.MethodGet, base+"/devices", nil, token)
	if err := json.Unmarshal(out.Data, &tree); err != nil {
		t.Fatal(err)
	}
	if tree.Items[0]["name"] == "被篡改名" {
		t.Fatal("device token must not change name")
	}

	// 4. admin 可编辑基本信息（改名成功）；viewer 无权限 → 403
	adminToken := login(t, base, "admin", "admin123")
	out, status = doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"name": "边缘盒-改名"}, adminToken)
	if status != 200 || out.Code != 0 {
		t.Fatalf("admin edit name: %d %s", out.Code, out.Message)
	}
	if _, status := doJSON(t, http.MethodPost, base+"/users", map[string]any{
		"username": "viewer-up", "password": "viewer123", "role": 2, "status": 1,
	}, adminToken); status != 200 {
		t.Fatalf("create viewer: %d", status)
	}
	viewerToken := login(t, base, "viewer-up", "viewer123")
	if _, status := doJSON(t, http.MethodPut, fmt.Sprintf("%s/devices/%d", base, edgeID),
		map[string]any{"name": "x"}, viewerToken); status != http.StatusForbidden {
		t.Fatalf("viewer edit device should 403, got %d", status)
	}
}

// TestUpdateCustomerFields 回归：编辑档案完整字段（姓名/身份证/住址/状态等）
func TestUpdateCustomerFields(t *testing.T) {
	ts, close := newEncryptedTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"
	token := login(t, base, "admin", "admin123")

	// 创建顾客
	out, status := doJSON(t, http.MethodPost, base+"/customers", map[string]any{
		"person_type": 0, "name": "编辑前", "id_card_no": "110101199001011234",
		"face_feature": featureBase64(),
	}, token)
	if status != 200 {
		t.Fatalf("create: %d", status)
	}
	var created struct {
		CustomerID uint64 `json:"customer_id"`
	}
	_ = json.Unmarshal(out.Data, &created)

	// 编辑：改名 + 改身份证 + 地址 + 性别 + 状态（黑名单）
	_, status = doJSON(t, http.MethodPut, fmt.Sprintf("%s/customers/%d", base, created.CustomerID), map[string]any{
		"name": "编辑后", "id_card_no": "110101199202022345", "address": "某市某区",
		"gender": 1, "status": 1, "birth_date": "1992-02-02",
	}, token)
	if status != 200 {
		t.Fatalf("update: %d", status)
	}

	// 查询确认
	out, status = doJSON(t, http.MethodGet, base+"/customers", nil, token)
	if status != 200 {
		t.Fatalf("list: %d", status)
	}
	var list struct {
		Items []map[string]interface{} `json:"items"`
	}
	if err := json.Unmarshal(out.Data, &list); err != nil {
		t.Fatal(err)
	}
	item := list.Items[0]
	if item["name"] != "编辑后" || item["id_card_no"] != "110101199202022345" ||
		item["address"] != "某市某区" || item["gender"].(float64) != 1 ||
		item["status"].(float64) != 1 {
		t.Fatalf("updated fields mismatch: %+v", item)
	}
}

// TestStoresAndRecordQuery 回归：门店 CRUD + 识别记录查询
func TestStoresAndRecordQuery(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"
	token := login(t, base, "admin", "admin123")

	// 1. 门店 CRUD
	out, status := doJSON(t, http.MethodPost, base+"/stores", map[string]any{"name": "望京店", "address": "xx 路 1 号"}, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("create store: %d %s", out.Code, out.Message)
	}
	var st struct{ StoreID uint64 `json:"store_id"` }
	_ = json.Unmarshal(out.Data, &st)
	if st.StoreID == 0 {
		t.Fatal("store id missing")
	}
	_, status = doJSON(t, http.MethodPut, fmt.Sprintf("%s/stores/%d", base, st.StoreID),
		map[string]any{"name": "望京店2", "address": "xx 路 2 号"}, token)
	if status != 200 {
		t.Fatalf("update store: %d", status)
	}
	out, status = doJSON(t, http.MethodGet, base+"/stores", nil, token)
	if status != 200 {
		t.Fatalf("list stores: %d", status)
	}
	var list struct {
		Items []map[string]interface{} `json:"items"`
	}
	_ = json.Unmarshal(out.Data, &list)
	if len(list.Items) != 1 || list.Items[0]["name"] != "望京店2" {
		t.Fatalf("store list mismatch: %+v", list.Items)
	}

	// 2. 门店下有设备时删除被拒绝
	edgeID, _ := regEdge(t, base, "边缘盒-门店", st.StoreID)
	_, status = doJSON(t, http.MethodDelete, fmt.Sprintf("%s/stores/%d", base, st.StoreID), nil, token)
	if status != 400 {
		t.Fatalf("delete store with devices should 400, got %d", status)
	}

	// 3. 识别记录查询（含快照字段）
	feat := featureBase64()
	snap := base64.StdEncoding.EncodeToString([]byte("fake-jpeg"))
	_, status = doJSON(t, http.MethodPost, base+"/records/recognition/batch", map[string]any{
		"device_id": edgeID,
		"records": []map[string]any{
			{"track_id": "T-R1", "person_type": 0, "face_feature": feat, "direction": 0,
				"camera_id": "cam-9", "snapshot": snap, "snapshot_mime": "image/jpeg",
				"created_at": "2026-09-18T10:00:00Z"},
		},
	}, token)
	if status != 200 {
		t.Fatalf("batch: %d", status)
	}
	out, status = doJSON(t, http.MethodGet, base+"/records/recognition?device_id="+fmt.Sprint(edgeID), nil, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("query records: %d %s", out.Code, out.Message)
	}
	var q struct {
		Total int64                    `json:"total"`
		Items []map[string]interface{} `json:"items"`
	}
	_ = json.Unmarshal(out.Data, &q)
	if q.Total != 1 || q.Items[0]["snapshot"] != snap {
		t.Fatalf("record query mismatch: total=%d items=%+v", q.Total, q.Items)
	}

	// 4. viewer 可读记录但设备 token 被拒
	adminToken := login(t, base, "admin", "admin123")
	_ = adminToken
	if _, status := doJSON(t, http.MethodPost, base+"/users", map[string]any{
		"username": "viewer-rec", "password": "viewer123", "role": 2, "status": 1,
	}, token); status != 200 {
		t.Fatalf("create viewer: %d", status)
	}
	viewerToken := login(t, base, "viewer-rec", "viewer123")
	if _, status := doJSON(t, http.MethodGet, base+"/records/recognition", nil, viewerToken); status != 200 {
		t.Fatalf("viewer read records should 200, got %d", status)
	}
	devToken := "unused"
	_ = devToken
	// 设备 token 查询记录应 403：先取设备 token（regEdge 返回 token）
	_, devTok := regEdge(t, base, "边缘盒-不可查", 1)
	if _, status := doJSON(t, http.MethodGet, base+"/records/recognition", nil, devTok); status != http.StatusForbidden {
		t.Fatalf("device token query records should 403, got %d", status)
	}
}

// TestFlowStatsEnhance 回归：客流按摄像头拆分 + 唯一识别人数
func TestFlowStatsEnhance(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"
	token := login(t, base, "admin", "admin123")
	edgeID, _ := regEdge(t, base, "边缘盒-stats", 1)
	feat := featureBase64()

	// 上报：cam-1 进 2 条 / cam-2 进 1 条；其中 1 条命中顾客（建档后）
	recs := []map[string]any{
		{"track_id": "S1", "person_type": 0, "face_feature": feat, "direction": 0, "camera_id": "cam-1", "created_at": "2026-09-18T10:00:00Z"},
		{"track_id": "S2", "person_type": 0, "face_feature": feat, "direction": 0, "camera_id": "cam-1", "created_at": "2026-09-18T11:00:00Z"},
		{"track_id": "S3", "person_type": 0, "face_feature": feat, "direction": 1, "camera_id": "cam-2", "created_at": "2026-09-18T12:00:00Z"},
	}
	if _, status := doJSON(t, http.MethodPost, base+"/records/recognition/batch", map[string]any{"device_id": edgeID, "records": recs}, token); status != 200 {
		t.Fatalf("batch: %d", status)
	}
	// 建档命中 S1/S2/S3 中一条（创建顾客回查只返回聚合，不影响记录 customer_id；直接更新一条记录命中）
	_, status := doJSON(t, http.MethodPost, base+"/customers", map[string]any{
		"person_type": 0, "name": "统计人", "id_card_no": "110101199001011299", "face_feature": feat,
	}, token)
	if status != 200 {
		t.Fatalf("create customer: %d", status)
	}

	// 按摄像头拆分
	out, status := doJSON(t, http.MethodGet, base+"/stats/flow?granularity=day&start_at=2026-09-01T00:00:00Z&group_by=camera&unique=1", nil, token)
	if status != 200 || out.Code != 0 {
		t.Fatalf("flow group: %d %s", out.Code, out.Message)
	}
	var g struct {
		Items []map[string]interface{} `json:"items"`
		Total struct {
			In            int64 `json:"in"`
			Out           int64 `json:"out"`
			UniquePersons int64 `json:"unique_persons"`
		} `json:"total"`
	}
	_ = json.Unmarshal(out.Data, &g)
	if len(g.Items) != 2 {
		t.Fatalf("expect 2 camera buckets, got %+v", g.Items)
	}
	if g.Total.In != 2 || g.Total.Out != 1 {
		t.Fatalf("total mismatch: %+v", g.Total)
	}
}

// TestCSVExport 回归：客流/人员 CSV 导出
func TestCSVExport(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	base := ts.URL + "/api/v1"
	token := login(t, base, "admin", "admin123")

	// 准备数据：门店 + 人员
	_, status := doJSON(t, http.MethodPost, base+"/stores", map[string]any{"name": "导出店"}, token)
	if status != 200 {
		t.Fatalf("create store: %d", status)
	}
	_, status = doJSON(t, http.MethodPost, base+"/customers", map[string]any{
		"person_type": 0, "name": "导出人", "id_card_no": "110101199001011211", "face_feature": featureBase64(),
	}, token)
	if status != 200 {
		t.Fatalf("create customer: %d", status)
	}

	// 客流 CSV：直接访问导出端点拿原始响应（不走 doJSON 包装）
	req, _ := http.NewRequest(http.MethodGet, base+"/export/flow.csv?granularity=day&start_at=2026-09-01T00:00:00Z", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("flow csv: %v", err)
	}
	defer resp.Body.Close()
	buf := new(bytes.Buffer)
	_, _ = buf.ReadFrom(resp.Body)
	if resp.StatusCode != 200 || !strings.Contains(buf.String(), "bucket,in,out") {
		t.Fatalf("flow csv invalid: status=%d body=%q", resp.StatusCode, buf.String())
	}

	// 人员 CSV
	req2, _ := http.NewRequest(http.MethodGet, base+"/export/customers.csv", nil)
	req2.Header.Set("Authorization", "Bearer "+token)
	resp2, err := http.DefaultClient.Do(req2)
	if err != nil {
		t.Fatalf("customers csv: %v", err)
	}
	defer resp2.Body.Close()
	buf2 := new(bytes.Buffer)
	_, _ = buf2.ReadFrom(resp2.Body)
	if resp2.StatusCode != 200 || !strings.Contains(buf2.String(), "导出人") {
		t.Fatalf("customers csv invalid: status=%d body=%q", resp2.StatusCode, buf2.String())
	}

	// 设备 token 导出 → 403
	_, devTok := regEdge(t, base, "边缘盒-导出", 1)
	req3, _ := http.NewRequest(http.MethodGet, base+"/export/flow.csv?start_at=2026-09-01T00:00:00Z", nil)
	req3.Header.Set("Authorization", "Bearer "+devTok)
	resp3, err := http.DefaultClient.Do(req3)
	if err != nil {
		t.Fatalf("device export: %v", err)
	}
	defer resp3.Body.Close()
	if resp3.StatusCode != http.StatusForbidden {
		t.Fatalf("device token export should 403, got %d", resp3.StatusCode)
	}
}
