package api_test

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"

	"github.com/xuri/excelize/v2"
)

// mockFaceService 模拟 face-service：返回固定 512 维特征
func mockFaceService(t *testing.T) *httptest.Server {
	t.Helper()
	feat := make([]byte, 512*4) // 合法长度（全 0）
	b64 := base64.StdEncoding.EncodeToString(feat)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/api/health":
			_, _ = w.Write([]byte(`{"code":0,"message":"ok","data":{"engine":"mock","dim":512}}`))
		case "/api/face/extract":
			_, _ = w.Write([]byte(fmt.Sprintf(
				`{"code":0,"message":"ok","data":{"feature_b64":%q,"dim":512,"faces":1,"engine":"mock"}}`, b64)))
		default:
			w.WriteHeader(404)
		}
	}))
	return srv
}

// writeSamplePNG 生成标准 PNG 写入临时文件（模板/导入测试用）
func writeSamplePNG(t *testing.T, path string) {
	t.Helper()
	img := image.NewRGBA(image.Rect(0, 0, 32, 32))
	for y := 0; y < 32; y++ {
		for x := 0; x < 32; x++ {
			img.Set(x, y, color.RGBA{R: 200, G: 210, B: 230, A: 255})
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatalf("png encode: %v", err)
	}
	if err := os.WriteFile(path, buf.Bytes(), 0o644); err != nil {
		t.Fatalf("write png: %v", err)
	}
}

// makeImportXLSX 生成 xlsx：表头 + 1 行顾客（门店=测试门店、主照片 K2 + 附加照片 L2）
func makeImportXLSX(t *testing.T) []byte {
	t.Helper()
	pngPath := t.TempDir() + "/face.png"
	writeSamplePNG(t, pngPath)

	f := excelize.NewFile()
	sheet := "Sheet1"
	header := []string{"人员类型", "姓名", "身份证号", "工号", "部门", "性别", "出生日期", "住址",
		"门店", "状态", "头像", "附加照片1", "附加照片2"}
	for i, h := range header {
		cell, _ := excelize.ColumnNumberToName(i + 1)
		_ = f.SetCellValue(sheet, cell+"1", h)
	}
	row := []string{"顾客", "测试导入", "110101199901011111", "", "", "男", "1999-01-01", "测试地址", "测试门店", "正常", "", "", ""}
	for i, v := range row {
		cell, _ := excelize.ColumnNumberToName(i + 1)
		_ = f.SetCellValue(sheet, cell+"2", v)
	}
	// 主照片+附加照片（多照片）
	if err := f.AddPicture(sheet, "K2", pngPath, &excelize.GraphicOptions{}); err != nil {
		t.Fatalf("add picture K2: %v", err)
	}
	if err := f.AddPicture(sheet, "L2", pngPath, &excelize.GraphicOptions{}); err != nil {
		t.Fatalf("add picture L2: %v", err)
	}
	_ = f.SetRowHeight(sheet, 2, 60)

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("write xlsx: %v", err)
	}
	return buf.Bytes()
}

// uploadFile 以 multipart 上传文件
func uploadFile(t *testing.T, url, field, filename string, content []byte, token string) (respEnvelope, int) {
	t.Helper()
	var buf bytes.Buffer
	w := multipart.NewWriter(&buf)
	fw, err := w.CreateFormFile(field, filename)
	if err != nil {
		t.Fatalf("create form file: %v", err)
	}
	if _, err := fw.Write(content); err != nil {
		t.Fatalf("write form: %v", err)
	}
	_ = w.Close()

	req, err := http.NewRequest(http.MethodPost, url, &buf)
	if err != nil {
		t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", w.FormDataContentType())
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

// downloadRaw 下载返回原始字节
func downloadRaw(t *testing.T, url, token string) ([]byte, int) {
	t.Helper()
	req, _ := http.NewRequest(http.MethodGet, url, nil)
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("do: %v", err)
	}
	defer resp.Body.Close()
	var buf bytes.Buffer
	_, _ = buf.ReadFrom(resp.Body)
	return buf.Bytes(), resp.StatusCode
}

func TestSystemConfig(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	token := login(t, ts.URL+"/api/v1", "admin", "admin123")
	base := ts.URL + "/api/v1"

	// 默认未配置
	out, st := doJSON(t, http.MethodGet, base+"/system/config", nil, token)
	if st != 200 {
		t.Fatalf("get config status = %d", st)
	}
	var cfg struct {
		FaceServiceURL     string `json:"face_service_url"`
		FaceServiceKeySet  bool   `json:"face_service_key_set"`
		EffectiveURL       string `json:"effective_url"`
		EffectiveKeyUsed   bool   `json:"effective_key_used"`
		FaceServiceDefault string `json:"face_service_default"`
	}
	_ = json.Unmarshal(out.Data, &cfg)
	if cfg.FaceServiceURL != "" || cfg.FaceServiceKeySet {
		t.Fatalf("default should be empty, got %+v", cfg)
	}
	if cfg.EffectiveURL != "http://127.0.0.1:8090" {
		t.Fatalf("effective url = %s", cfg.EffectiveURL)
	}

	// 保存配置
	_, st = doJSON(t, http.MethodPut, base+"/system/config",
		map[string]string{"face_service_url": "http://face:9000", "face_service_key": "sk"}, token)
	if st != 200 {
		t.Fatalf("put config status = %d", st)
	}
	out, _ = doJSON(t, http.MethodGet, base+"/system/config", nil, token)
	_ = json.Unmarshal(out.Data, &cfg)
	if cfg.FaceServiceURL != "http://face:9000" || !cfg.FaceServiceKeySet || cfg.EffectiveURL != "http://face:9000" {
		t.Fatalf("after put = %+v", cfg)
	}

	// 普通用户（非 admin）不可读写系统配置
	_, _ = doJSON(t, http.MethodPut, base+"/system/config",
		map[string]string{"face_service_url": "x"}, "bad-token")
}

func TestImportTemplateAndImport(t *testing.T) {
	fs := mockFaceService(t)
	defer fs.Close()

	ts, close := newTestServer(t)
	defer close()
	token := login(t, ts.URL+"/api/v1", "admin", "admin123")
	base := ts.URL + "/api/v1"

	// 配置 face-service 指向 mock
	_, st := doJSON(t, http.MethodPut, base+"/system/config",
		map[string]string{"face_service_url": fs.URL, "face_service_key": "k"}, token)
	if st != 200 {
		t.Fatalf("put config status = %d", st)
	}

	// 下载模板：应为 xlsx 且含嵌入图片
	tpl, st := downloadRaw(t, base+"/customers/import/template", token)
	if st != 200 {
		t.Fatalf("template status = %d", st)
	}
	f, err := excelize.OpenReader(bytes.NewReader(tpl))
	if err != nil {
		t.Fatalf("template not xlsx: %v", err)
	}
	pics, err := f.GetPictures(f.GetSheetName(0), "K2")
	if err != nil || len(pics) == 0 {
		t.Fatalf("template should embed sample image in K2, pics=%d err=%v", len(pics), err)
	}
	if got := len(f.GetSheetList()); got < 2 {
		t.Fatalf("template sheets = %v", f.GetSheetList())
	}
	_ = f.Close()

	// 导入：xlsx（1 行顾客，I2 嵌入图片）
	xlsx := makeImportXLSX(t)
	out, st := uploadFile(t, base+"/customers/import", "file", "import.xlsx", xlsx, token)
	if st != 200 {
		t.Fatalf("import status = %d body=%s", st, string(out.Data))
	}
	var res struct {
		Total   int `json:"total"`
		Success int `json:"success"`
		Failed  []struct {
			Row    int    `json:"row"`
			Name   string `json:"name"`
			Reason string `json:"reason"`
		} `json:"failed"`
	}
	if err := json.Unmarshal(out.Data, &res); err != nil {
		t.Fatalf("decode import result: %v", err)
	}
	if res.Total != 1 || res.Success != 1 || len(res.Failed) != 0 {
		t.Fatalf("import result = %+v", res)
	}

	// 门店应被自动创建
	out2, st2 := doJSON(t, http.MethodGet, base+"/stores?name="+"%E6%B5%8B%E8%AF%95%E9%97%A8%E5%BA%97", nil, token)
	if st2 != 200 {
		t.Fatalf("list stores status = %d", st2)
	}
	var stores struct {
		Items []struct {
			ID   uint64 `json:"id"`
			Name string `json:"name"`
		} `json:"items"`
	}
	_ = json.Unmarshal(out2.Data, &stores)
	if len(stores.Items) == 0 || stores.Items[0].Name != "测试门店" {
		t.Fatalf("store not auto-created: %+v", stores.Items)
	}

	// 再次导入相同证件号 → 应失败（身份证重复）
	out, st = uploadFile(t, base+"/customers/import", "file", "import2.xlsx", xlsx, token)
	_ = json.Unmarshal(out.Data, &res)
	if st != 200 || res.Success != 0 || len(res.Failed) != 1 {
		t.Fatalf("duplicate import should fail, got %+v st=%d", res, st)
	}
}

func TestImportMissingRequiredColumn(t *testing.T) {
	ts, close := newTestServer(t)
	defer close()
	token := login(t, ts.URL+"/api/v1", "admin", "admin123")
	base := ts.URL + "/api/v1"

	// 无“姓名”列的表
	f := excelize.NewFile()
	_ = f.SetCellValue("Sheet1", "A1", "人员类型")
	_ = f.SetCellValue("Sheet1", "A2", "顾客")
	var buf bytes.Buffer
	_ = f.Write(&buf)

	out, st := uploadFile(t, base+"/customers/import", "file", "bad.xlsx", buf.Bytes(), token)
	if st != 400 {
		t.Fatalf("want 400, got %d body=%s", st, string(out.Data))
	}
}