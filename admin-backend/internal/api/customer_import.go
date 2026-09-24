package api

import (
	"bytes"
	"context"
	"encoding/base64"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"admin-backend/internal/faceservice"
	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
	"github.com/xuri/excelize/v2"
	"gorm.io/gorm"
)

// 导入模板列名（表头）。解析时按表头名映射，支持列顺序调整。
// 照片列为图片列：主"头像" + 可选"附加照片1/2"（多照片→多特征）。
var importCols = []string{
	"人员类型", "姓名", "身份证号", "工号", "部门", "性别", "出生日期", "住址",
	"门店", "状态", "头像", "附加照片1", "附加照片2",
}

// photoCols 图片列（按优先级：头像为主特征，其余为附加特征）
var photoCols = []string{"头像", "附加照片1", "附加照片2"}

// importColumnIndex 表头行到列索引映射（name -> 列号，1 起始）
func importColumnIndex(header []string) map[string]int {
	m := make(map[string]int, len(importCols))
	for i, h := range header {
		h = strings.TrimSpace(h)
		for _, col := range importCols {
			if h == col {
				m[col] = i + 1
				break
			}
		}
	}
	return m
}

// GET /customers/import/template 下载 Excel 导入模板（admin/operator）
func (s *Server) downloadImportTemplate(c *gin.Context) {
	f := excelize.NewFile()
	defer f.Close()

	sheet := "人员档案"
	if _, err := f.NewSheet(sheet); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	// 默认 Sheet1 删除，只留「人员档案」「填写说明」
	f.DeleteSheet("Sheet1")

	// 表头
	cols := []string{"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M"}
	sid, _ := headerStyle(f)
	for i, name := range importCols {
		cell := cols[i] + "1"
		_ = f.SetCellValue(sheet, cell, name)
		_ = f.SetCellStyle(sheet, cell, cell, sid)
	}
	_ = f.SetRowHeight(sheet, 1, 24)

	// 示例行：第2行顾客（头像+附加照片1 多照片）、第3行内部人员；示例门店
	exampleRows := [][]string{
		{"顾客", "张三", "110101199001011234", "", "", "男", "1990-01-01", "北京市朝阳区示例路1号", "示例门店", "正常"},
		{"内部人员", "李四", "", "E1024", "运营部", "女", "1992-02-02", "上海市浦东新区示例路2号", "示例门店", "正常"},
	}
	for i, row := range exampleRows {
		r := i + 2
		for j, v := range row {
			_ = f.SetCellValue(sheet, cols[j]+strconv.Itoa(r), v)
		}
		_ = f.SetCellValue(sheet, "K"+strconv.Itoa(r), "（在此嵌入正脸照片）")
		imgPath := writeTempPNG(fmt.Sprintf("template_sample_%d", r))
		if imgPath != "" {
			_ = f.AddPicture(sheet, "K"+strconv.Itoa(r), imgPath, &excelize.GraphicOptions{ScaleX: 0.6, ScaleY: 0.6})
		}
		// 第2行再演示一张附加照片
		if i == 0 {
			_ = f.SetCellValue(sheet, "L2", "（可选：附加照片）")
			if imgPath != "" {
				_ = f.AddPicture(sheet, "L2", imgPath, &excelize.GraphicOptions{ScaleX: 0.6, ScaleY: 0.6})
			}
		}
		_ = f.SetRowHeight(sheet, r, 90)
	}

	// 填写说明 sheet
	helpSheet := "填写说明"
	_, _ = f.NewSheet(helpSheet)
	helpLines := []string{
		"【FaceFlow 人员批量导入说明】",
		"",
		"1. 表头固定为：人员类型 / 姓名 / 身份证号 / 工号 / 部门 / 性别 / 出生日期 / 住址 /",
		"   门店 / 状态 / 头像 / 附加照片1 / 附加照片2，顺序可调整，但表头文字必须一致；",
		"   模板中的示例行请删除。",
		"2. 人员类型：顾客 或 内部人员。顾客必填身份证号；内部人员必填工号。",
		"3. 门店：填门店名称，已存在则自动关联；不存在则自动创建（状态=正常）。留空=未分配。",
		"4. 状态（可空）：正常 / 黑名单 / 注销 / 离职，留空=正常。",
		"5. 性别：男 / 女 / 未知（可空）。出生日期格式：YYYY-MM-DD。",
		"6. 照片列（Excel『插入 → 图片』嵌入单元格，jpg/png/bmp）：",
		"   - 头像：主照片，导入时自动调用 face-service 提取主特征（512 维）入库；",
		"   - 附加照片1/2：可选，分别提取为附加特征（多照片提升识别率）；",
		"   - 主照片提取失败该行失败；附加照片提取失败仅跳过该张，不影响整行。",
		"7. 无法识别人脸的照片该行会导入失败，可在结果中查看失败原因后修正重试。",
		"8. 导入入口：管理后台 → 人员管理 → 『模板导入』，选择本文件上传。",
		"",
		"【face-service 配置提示】",
		"如果上传后提示人脸识别服务不可用，请先在管理后台 → 系统配置中设置人脸识别服务地址与密钥。",
	}
	for i, line := range helpLines {
		_ = f.SetCellValue(helpSheet, "A"+strconv.Itoa(i+1), line)
	}
	_ = f.SetColWidth(helpSheet, "A", "A", 100)

	// 导出
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	c.Header("Content-Type", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")
	c.Header("Content-Disposition", `attachment; filename="faceflow_customers_import_template.xlsx"`)
	_, _ = c.Writer.Write(buf.Bytes())
	s.audit(c, "download_import_template", "system", 0, "")
	auditDone(c)
}

// makeSamplePNG 生成标准 PNG 示例图片（浅底+深色块，示意头像占位）
func makeSamplePNG() ([]byte, error) {
	img := image.NewRGBA(image.Rect(0, 0, 96, 96))
	for y := 0; y < 96; y++ {
		for x := 0; x < 96; x++ {
			img.Set(x, y, color.RGBA{R: 230, G: 235, B: 245, A: 255})
		}
	}
	for y := 18; y < 78; y++ {
		for x := 18; x < 78; x++ {
			img.Set(x, y, color.RGBA{R: 180, G: 190, B: 205, A: 255})
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// writeFileBytes 写临时文件（模板示例图）
func writeFileBytes(path string, data []byte) error {
	return os.WriteFile(path, data, 0o644)
}

// writeTempPNG 写入临时示例图片返回路径（失败返回空串）
func writeTempPNG(name string) string {
	data, err := makeSamplePNG()
	if err != nil {
		return ""
	}
	p := "/tmp/" + name + ".png"
	if err := writeFileBytes(p, data); err != nil {
		return ""
	}
	return p
}

// POST /customers/import 批量导入人员（admin/operator）
// 上传 Excel（.xlsx）：支持 门店/状态/多照片（头像+附加照片1/2 内嵌图片），
// 逐行调用 face-service 提取特征并入库；失败行不中断，返回成功/失败明细。
func (s *Server) importCustomers(c *gin.Context) {
	file, err := c.FormFile("file")
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "file field required: "+err.Error())
		return
	}
	if file.Size > 10<<20 {
		Fail(c, http.StatusBadRequest, CodeParam, "file too large (>10MB)")
		return
	}
	fh, err := file.Open()
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "open file: "+err.Error())
		return
	}
	defer fh.Close()

	f, err := excelize.OpenReader(fh)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, "not a valid xlsx: "+err.Error())
		return
	}
	defer f.Close()

	sheet := f.GetSheetName(0)
	rows, err := f.GetRows(sheet)
	if err != nil || len(rows) < 2 {
		Fail(c, http.StatusBadRequest, CodeParam, "sheet empty or no data rows")
		return
	}

	colsIdx := importColumnIndex(rows[0])
	for _, n := range []string{"人员类型", "姓名"} {
		if colsIdx[n] == 0 {
			Fail(c, http.StatusBadRequest, CodeParam, "missing required column: "+n)
			return
		}
	}

	cli := faceservice.New(s.faceServiceCfg())
	ctx := c.Request.Context()
	type failRow struct {
		Row    int    `json:"row"`
		Name   string `json:"name"`
		Reason string `json:"reason"`
	}
	failed := make([]failRow, 0)
	success := 0
	storeCache := make(map[string]uint64) // 门店名称->id（避免重复建店）

	for i := 1; i < len(rows); i++ { // 跳过表头
		rowNo := i + 1
		vals := rows[i]
		cell := func(name string) string {
			col := colsIdx[name]
			if col == 0 || col > len(vals) {
				return ""
			}
			return strings.TrimSpace(vals[col-1])
		}
		name := cell("姓名")
		if name == "" {
			failed = append(failed, failRow{rowNo, "", "姓名为空"})
			continue
		}

		// 门店：名称匹配，不存在自动创建
		storeID := uint64(0)
		if storeName := cell("门店"); storeName != "" {
			sid, err := s.resolveStoreByName(ctx, storeName, storeCache)
			if err != nil {
				failed = append(failed, failRow{rowNo, name, "门店处理失败：" + err.Error()})
				continue
			}
			storeID = sid
		}

		// 状态
		status, statusOK := parseStatus(cell("状态"))
		if !statusOK {
			failed = append(failed, failRow{rowNo, name, "状态无效：" + cell("状态")})
			continue
		}

		// 多照片：按优先级取（主头像 > 附加照片1 > 附加照片2）
		imgs := rowImages(f, sheet, rowNo, colsIdx, vals)
		feat, extraFeats, failReason := s.extractRowFeatures(ctx, cli, imgs)
		if failReason != "" {
			failed = append(failed, failRow{rowNo, name, failReason})
			continue
		}

		// 类型/必填校验
		var personType int8
		switch cell("人员类型") {
		case "", "顾客", "客户", "0":
			personType = storage.PersonTypeCustomer
		case "内部人员", "员工", "1":
			personType = storage.PersonTypeStaff
		default:
			failed = append(failed, failRow{rowNo, name, "人员类型无效：" + cell("人员类型")})
			continue
		}
		idCard := cell("身份证号")
		staffNo := cell("工号")
		if personType == storage.PersonTypeCustomer && idCard == "" {
			failed = append(failed, failRow{rowNo, name, "顾客必填身份证号"})
			continue
		}
		if personType == storage.PersonTypeStaff && staffNo == "" {
			failed = append(failed, failRow{rowNo, name, "内部人员必填工号"})
			continue
		}

		now := time.Now().Unix()
		cust := storage.Customer{
			PersonType:    personType,
			StoreID:       storeID,
			StaffNo:       strPtr(staffNo),
			Department:    cell("部门"),
			Gender:        parseGender(cell("性别")),
			FaceFeature:   feat,
			Status:        status,
			Version:       1,
			CreatedBy:     c.GetString(ctxUserID),
			CreatedAt:     now,
			UpdatedAt:     now,
		}
		if bd := cell("出生日期"); bd != "" {
			if t, e := time.Parse("2006-01-02", bd); e == nil {
				cust.BirthDate = t.Unix()
			}
		}
		// 敏感字段加密
		if cust.NameEnc, err = s.cip.Encrypt(name); err != nil {
			failed = append(failed, failRow{rowNo, name, "加密失败：" + err.Error()})
			continue
		}
		if idCard != "" {
			if cust.IDCardNoEnc, err = s.cip.Encrypt(idCard); err != nil {
				failed = append(failed, failRow{rowNo, name, "加密失败：" + err.Error()})
				continue
			}
		}
		if cust.AddressEnc, err = s.cip.Encrypt(cell("住址")); err != nil {
			failed = append(failed, failRow{rowNo, name, "加密失败：" + err.Error()})
			continue
		}

		if err := s.db.Create(&cust).Error; err != nil {
			reason := err.Error()
			if strings.Contains(reason, "UNIQUE") || strings.Contains(reason, "Duplicate") {
				reason = "身份证号/工号重复"
			}
			failed = append(failed, failRow{rowNo, name, reason})
			continue
		}
		// 主照片转存（对象存储开启时）
		if imgs[0].bytes != nil {
			cust.IDPhotoPath = s.saveImage(ctx, base64.StdEncoding.EncodeToString(imgs[0].bytes), "id_photos")
			_ = s.db.Model(&storage.Customer{}).Where("id = ?", cust.ID).Update("id_photo_path", cust.IDPhotoPath)
		}
		// 附加特征入库
		for _, ef := range extraFeats {
			_ = s.db.Create(&storage.CustomerFeature{CustomerID: cust.ID, FaceFeature: ef, Source: 0})
		}
		success++
	}

	s.audit(c, "import_customers", "system", 0,
		fmt.Sprintf("total=%d success=%d failed=%d", len(rows)-1, success, len(failed)))
	auditDone(c)
	OK(c, gin.H{
		"total":   len(rows) - 1,
		"success": success,
		"failed":  failed,
	})
}

// rowImage 单列照片（兼容旧测试/简单场景）
type rowImage struct {
	col   string
	bytes []byte
}

// rowImages 读取一行所有照片列（头像/附加照片1/附加照片2）的嵌入图片，
// 按 photoCols 优先级排序。无图则 bytes == nil。
func rowImages(f *excelize.File, sheet string, rowNo int, colsIdx map[string]int, vals []string) []rowImage {
	out := make([]rowImage, 0, len(photoCols))
	for _, colName := range photoCols {
		col := colsIdx[colName]
		if col == 0 {
			continue
		}
		img := readCellImage(f, sheet, rowNo, col)
		out = append(out, rowImage{col: colName, bytes: img})
	}
	// 为保证 imgs[0] 总是主照片，无图列用空占位
	for len(out) < len(photoCols) {
		out = append(out, rowImage{})
	}
	return out
}

// readCellImage 读取指定列单元格嵌入的第一张图片
func readCellImage(f *excelize.File, sheet string, rowNo, col int) []byte {
	if col <= 0 {
		return nil
	}
	colName, err := excelize.ColumnNumberToName(col)
	if err != nil {
		return nil
	}
	pics, err := f.GetPictures(sheet, colName+strconv.Itoa(rowNo))
	if err != nil || len(pics) == 0 {
		return nil
	}
	return pics[0].File
}

// extractRowFeatures 提取一行特征：imgs[0] 为主特征（失败→整行失败），
// 其余为附加特征（失败仅跳过该张）。
func (s *Server) extractRowFeatures(ctx context.Context, cli *faceservice.Client, imgs []rowImage) (feat []byte, extra [][]byte, failReason string) {
	first := true
	for _, img := range imgs {
		if img.bytes == nil {
			continue
		}
		f, err := cli.Extract(ctx, img.bytes, "image/jpeg")
		if err != nil {
			reason := err.Error()
			if errors.Is(err, faceservice.ErrNoFace) {
				reason = "照片(" + img.col + ")未检测到人脸"
			} else if errors.Is(err, faceservice.ErrServiceUnavailable) {
				reason = "人脸识别服务不可用（请检查系统配置）"
			}
			if first {
				return nil, nil, reason
			}
			continue // 附加照片失败跳过
		}
		if first {
			feat = f
			first = false
		} else {
			extra = append(extra, f)
		}
	}
	return feat, extra, ""
}

// resolveStoreByName 按名称匹配门店；不存在则自动创建（名称不区分大小写匹配 exact）。
func (s *Server) resolveStoreByName(ctx context.Context, name string, cache map[string]uint64) (uint64, error) {
	if id, ok := cache[name]; ok {
		return id, nil
	}
	var st storage.Store
	err := s.db.WithContext(ctx).Where("name = ?", name).First(&st).Error
	if err == nil {
		cache[name] = st.ID
		return st.ID, nil
	}
	if !errors.Is(err, gorm.ErrRecordNotFound) {
		return 0, err
	}
	now := time.Now().Unix()
	st = storage.Store{Name: name, Status: 1, CreatedAt: now, UpdatedAt: now}
	if err := s.db.WithContext(ctx).Create(&st).Error; err != nil {
		if strings.Contains(err.Error(), "UNIQUE") || strings.Contains(err.Error(), "Duplicate") {
			// 并发创建兜底：再查一次
			if err2 := s.db.WithContext(ctx).Where("name = ?", name).First(&st).Error; err2 == nil {
				cache[name] = st.ID
				return st.ID, nil
			}
		}
		return 0, err
	}
	cache[name] = st.ID
	return st.ID, nil
}

// parseStatus 解析状态文本
func parseStatus(v string) (int8, bool) {
	switch strings.TrimSpace(v) {
	case "", "正常", "0":
		return 0, true
	case "黑名单", "1":
		return 1, true
	case "注销", "2":
		return 2, true
	case "离职", "3":
		return 3, true
	default:
		return 0, false
	}
}

// parseGender 解析性别文本
func parseGender(v string) int8 {
	switch strings.TrimSpace(v) {
	case "男", "1":
		return 1
	case "女", "2":
		return 2
	default:
		return 0
	}
}

// headerStyle 表头加粗灰色底样式
func headerStyle(f *excelize.File) (int, error) {
	styleID, err := f.NewStyle(&excelize.Style{
		Font:      &excelize.Font{Bold: true},
		Fill:      excelize.Fill{Type: "pattern", Color: []string{"D9E1F2"}, Pattern: 1},
		Alignment: &excelize.Alignment{Horizontal: "center", Vertical: "center"},
	})
	return styleID, err
}