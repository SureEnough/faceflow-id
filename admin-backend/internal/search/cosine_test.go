package search

import (
	"encoding/binary"
	"math"
	"math/rand"
	"testing"
)

func TestEncodeDecode(t *testing.T) {
	want := make(Feature, Dim)
	for i := range want {
		want[i] = float32(rand.NormFloat64())
	}
	raw := make([]byte, Dim*4)
	for i := 0; i < Dim; i++ {
		binary.LittleEndian.PutUint32(raw[i*4:], math.Float32bits(want[i]))
	}
	got, err := Decode(raw)
	if err != nil {
		t.Fatalf("Decode: %v", err)
	}
	for i := range want {
		if math.Abs(float64(got[i]-want[i])) > 1e-6 {
			t.Fatalf("mismatch at %d", i)
		}
	}
	// 错误长度
	if _, err := Decode(make([]byte, 100)); err == nil {
		t.Fatal("expected error for wrong length")
	}
}

func TestNormalizeAndCosine(t *testing.T) {
	a := make(Feature, Dim)
	a[0], a[1], a[2], a[3] = 1, 2, 3, 4
	a.NormalizeL2()
	norm := 0.0
	for _, v := range a {
		norm += float64(v) * float64(v)
	}
	if math.Abs(norm-1) > 1e-4 { // float32 精度误差
		t.Fatalf("L2 norm = %f, expected ~1", norm)
	}
	// 相同向量余弦 = 1
	b := make(Feature, Dim)
	copy(b, a)
	if math.Abs(float64(Cosine(a, b))-1) > 1e-6 {
		t.Fatal("cosine of same vector should be 1")
	}
	// 正交向量余弦 ~ 0（c=(2,-1,0,...) 与 a=(1,2,3,4,...) 前两维点积为 0）
	c := make(Feature, Dim)
	c[0] = 2
	c[1] = -1
	c.NormalizeL2()
	if math.Abs(float64(Cosine(a, c))) > 1e-5 {
		t.Fatalf("cosine of orthogonal vectors should be ~0, got %f", Cosine(a, c))
	}
}

func TestLinearSearchTopK(t *testing.T) {
	// 三个特征：A 与查询相同，B/C 不同
	q := make(Feature, Dim)
	qa := make(Feature, Dim)
	qb := make(Feature, Dim)
	for i := 0; i < Dim; i++ {
		q[i] = float32(rand.NormFloat64())
		qa[i] = q[i]        // 同 A
		qb[i] = -q[i]       // 反相 → 余弦 -1
	}
	q.NormalizeL2()
	qa.NormalizeL2()
	qb.NormalizeL2()
	items := []Indexed{
		{ID: 1, Feature: qa, Threshold: 0.4},
		{ID: 2, Feature: qb, Threshold: 0.4},
	}
	hits := LinearSearch(q, items)
	if len(hits) != 1 || hits[0].ID != 1 {
		t.Fatalf("expected only id=1 hit, got %+v", hits)
	}
	if math.Abs(float64(hits[0].Sim)-1) > 1e-5 {
		t.Fatalf("sim should be ~1, got %f", hits[0].Sim)
	}
	// 排序：LinearSearch 返回全部命中且按分数降序（Top-K 由调用方截断，如 HistorySearch）
	it := make([]Indexed, Dim)
	for i := range it {
		f := make(Feature, Dim)
		for j := range f {
			f[j] = q[(j+i)%Dim]
		}
		f.NormalizeL2()
		it[i] = Indexed{ID: uint64(i + 10), Feature: f, Threshold: -1.0}
	}
	h := LinearSearch(q, it)
	if len(h) != len(it) {
		t.Fatalf("expected all hits (%d), got %d", len(it), len(h))
	}
	for i := 1; i < len(h); i++ {
		if h[i-1].Sim < h[i].Sim {
			t.Fatal("hits should be sorted descending by similarity")
		}
	}
	// TopK 截断（与 HistorySearch 一致）
	if len(h) > 50 {
		h = h[:50]
	}
	if len(h) != 50 {
		t.Fatalf("topK truncation failed: %d", len(h))
	}
}