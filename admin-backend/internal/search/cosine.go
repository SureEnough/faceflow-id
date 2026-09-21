package search

import (
	"encoding/binary"
	"errors"
	"math"
	"sort"
)

// 特征维度：ArcFace 512 维 float32
const Dim = 512

var errBadLen = errors.New("feature length must be 2048 bytes")

// Feature 人脸特征向量（float32[512]）
type Feature []float32

// Decode base64 原始字节 → Feature（doc 约定 base64(2048B)）
func Decode(b []byte) (Feature, error) {
	if len(b) != Dim*4 {
		return nil, errBadLen
	}
	f := make(Feature, Dim)
	for i := 0; i < Dim; i++ {
		bits := binary.LittleEndian.Uint32(b[i*4 : i*4+4])
		f[i] = math.Float32frombits(bits)
	}
	return f, nil
}

// NormalizeL2 原地 L2 归一化
func (f Feature) NormalizeL2() {
	var sum float64
	for _, v := range f {
		sum += float64(v) * float64(v)
	}
	norm := math.Sqrt(sum)
	if norm < 1e-12 {
		return
	}
	for i := range f {
		f[i] = float32(float64(f[i]) / norm)
	}
}

// Cosine 余弦相似度（要求两向量均已归一化，则内积即余弦）
func Cosine(a, b Feature) float32 {
	var dot float32
	for i := 0; i < Dim && i < len(a) && i < len(b); i++ {
		dot += a[i] * b[i]
	}
	if dot > 1 {
		dot = 1
	} else if dot < -1 {
		dot = -1
	}
	return dot
}

// Hit 检索命中项
type Hit struct {
	ID    uint64
	Sim   float32
	Extra map[string]any
}

// LinearSearch 线性扫描 Top-K（数据量 < 10 万条时足够；返回按相似度降序）
func LinearSearch(q Feature, items []Indexed) []Hit {
	hits := make([]Hit, 0, len(items))
	for _, it := range items {
		if len(it.Feature) == 0 {
			continue
		}
		sim := Cosine(q, it.Feature)
		if sim >= it.Threshold {
			hits = append(hits, Hit{ID: it.ID, Sim: sim, Extra: it.Extra})
		}
	}
	sort.Slice(hits, func(i, j int) bool { return hits[i].Sim > hits[j].Sim })
	return hits
}

// Indexed 可检索项（由存储层填充 Extra，例如 CreatedAt、DeviceID、TrackID）
type Indexed struct {
	ID        uint64
	Feature   Feature
	Threshold float32
	Extra     map[string]any
}

// --- FAISS 预留接口 ---
// 数据量 ≥ 10 万条时，可经 cgo 集成 FAISS（IndexFlatIP）：
// 1. 将全部 Feature 批量写入 IndexFlatIP（先 NormalizeL2）
// 2. Search(q, topK) 返回 id 列表
// 3. 回表取记录与聚合
// SearchIndex 接口保持业务层稳定，便于后续替换实现。

type SearchIndex interface {
	Add(uid uint64, f Feature)
	Search(q Feature, topK int) []uint64
	Size() int
}

// MemIndex 内存实现（开发/中小规模）
type MemIndex struct {
	ids    []uint64
	feats  []Feature
	thresh float32
}

func NewMemIndex(thresh float32) *MemIndex {
	return &MemIndex{thresh: thresh}
}

func (m *MemIndex) Add(uid uint64, f Feature) {
	nf := make(Feature, len(f))
	copy(nf, f)
	nf.NormalizeL2()
	m.ids = append(m.ids, uid)
	m.feats = append(m.feats, nf)
}

func (m *MemIndex) Search(q Feature, topK int) []uint64 {
	qn := make(Feature, len(q))
	copy(qn, q)
	qn.NormalizeL2()
	item := make([]Indexed, 0, len(m.ids))
	for i := range m.ids {
		item = append(item, Indexed{ID: m.ids[i], Feature: m.feats[i], Threshold: m.thresh})
	}
	hits := LinearSearch(qn, item)
	if len(hits) > topK {
		hits = hits[:topK]
	}
	out := make([]uint64, 0, len(hits))
	for _, h := range hits {
		out = append(out, h.ID)
	}
	return out
}

func (m *MemIndex) Size() int { return len(m.ids) }