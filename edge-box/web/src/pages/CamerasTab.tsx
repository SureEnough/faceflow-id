import { useEffect, useMemo, useState } from 'react'
import {
  Button,
  Card,
  Col,
  Empty,
  Form,
  Input,
  InputNumber,
  Modal,
  Popconfirm,
  Row,
  Select,
  Space,
  Switch,
  message,
} from 'antd'
import { DeleteOutlined, EyeOutlined, PlusOutlined } from '@ant-design/icons'
import { fetchConfig, fetchPreview, saveConfig, triggerReload } from '../api'
import type { PreviewFrame } from '../api'
import type { CameraCfg } from '../types'

interface InstrumentedCam extends CameraCfg {
  key: number
}

export default function CamerasTab({ onSaved }: { onSaved: () => void }) {
  const [rows, setRows] = useState<InstrumentedCam[]>([])
  const [loading, setLoading] = useState(false)
  const [saving, setSaving] = useState(false)
  const [apply, setApply] = useState(false)

  useEffect(() => {
    setLoading(true)
    fetchConfig()
      .then((cfg) => setRows(cfg.cameras.map((c, i) => ({ ...c, key: i }))))
      .catch(() => message.error('配置获取失败'))
      .finally(() => setLoading(false))
  }, [])

  const dirty = useMemo(() => rows.some((r) => !r.camera_id || !r.url), [rows])

  // ---- 画面预览 ----
  const [previewCam, setPreviewCam] = useState<InstrumentedCam | null>(null)
  const [frame, setFrame] = useState<PreviewFrame | null>(null)
  const [previewErr, setPreviewErr] = useState<string | null>(null)

  useEffect(() => {
    if (!previewCam) return
    let alive = true
    let timer: number | undefined
    const tick = async () => {
      try {
        const f = await fetchPreview(previewCam.camera_id)
        if (!alive) return
        setFrame(f)
        setPreviewErr(null)
      } catch (e) {
        if (!alive) return
        // 404 = 该相机暂无缓存帧（摄像头未打开/未采集）
        setPreviewErr((e as Error)?.message || '无画面')
      }
    }
    tick()
    timer = window.setInterval(tick, 1500)
    return () => {
      alive = false
      if (timer) window.clearInterval(timer)
    }
  }, [previewCam])

  const closePreview = () => {
    setPreviewCam(null)
    setFrame(null)
    setPreviewErr(null)
  }

  const addRow = () => {
    const next = [...rows, {
      key: Date.now(), camera_id: '', url: 'rtsp://', role: 'entrance',
      count_flow: true, direction: 'both',
      virtual_line: { x1: 0, y1: 0, x2: 0, y2: 0 },
    }]
    setRows(next)
  }
  const removeRow = (key: number) => setRows(rows.filter((r) => r.key !== key))
  const patchRow = (key: number, patch: Partial<InstrumentedCam>) =>
    setRows(rows.map((r) => (r.key === key ? { ...r, ...patch } : r)))

  const save = async (reload: boolean) => {
    setSaving(true)
    try {
      await saveConfig({ cameras: rows })
      message.success('摄像头已保存')
      if (reload) {
        await triggerReload()
        message.success('已应用')
      }
      onSaved()
    } catch (e) {
      message.error((e as Error).message || '保存失败')
    } finally {
      setSaving(false)
    }
  }

  return (
    <Card
      size="small"
      title="摄像头配置"
      loading={loading}
      extra={
        <Button icon={<PlusOutlined />} onClick={addRow}>
          添加摄像头
        </Button>
      }
    >
      {rows.map((r) => (
        <Card
          key={r.key}
          size="small"
          style={{ marginBottom: 12 }}
          title={`摄像头 ${r.camera_id || '(未命名)'}`}
          extra={
            <Space size={4}>
              <Button size="small" icon={<EyeOutlined />} disabled={!r.camera_id}
                      onClick={() => { setFrame(null); setPreviewErr(null); setPreviewCam(r) }}>
                预览
              </Button>
              <Popconfirm title="确认删除该摄像头？" onConfirm={() => removeRow(r.key)}>
                <Button size="small" danger icon={<DeleteOutlined />} />
              </Popconfirm>
            </Space>
          }
        >
          <Row gutter={12}>
            <Col span={5}>
              <Form.Item label="ID" required>
                <Input value={r.camera_id} onChange={(e) => patchRow(r.key, { camera_id: e.target.value })} />
              </Form.Item>
            </Col>
            <Col span={10}>
              <Form.Item label="URL（rtsp:// 或 usb0）" required>
                <Input value={r.url} onChange={(e) => patchRow(r.key, { url: e.target.value })} />
              </Form.Item>
            </Col>
            <Col span={4}>
              <Form.Item label="角色">
                <Select
                  value={r.role}
                  onChange={(v) => patchRow(r.key, { role: v })}
                  options={[{ value: 'entrance', label: 'entrance' }, { value: 'counter', label: 'counter' }]}
                />
              </Form.Item>
            </Col>
            <Col span={2}>
              <Form.Item label="计入客流" valuePropName="checked">
                <Switch size="small" checked={r.count_flow} onChange={(v) => patchRow(r.key, { count_flow: v })} />
              </Form.Item>
            </Col>
            <Col span={3}>
              <Form.Item label="判向">
                <Select
                  value={r.direction}
                  onChange={(v) => patchRow(r.key, { direction: v })}
                  options={[
                    { value: 'both', label: '双向' },
                    { value: 'in', label: '进' },
                    { value: 'out', label: '出' },
                  ]}
                />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={12}>
            <Col span={6}>
              <Form.Item label="虚拟线 起点 x,y">
                <Space.Compact style={{ width: '100%' }}>
                  <InputNumber
                    style={{ width: '50%' }}
                    value={r.virtual_line.x1}
                    onChange={(v) => patchRow(r.key, { virtual_line: { ...r.virtual_line, x1: v ?? 0 } })}
                  />
                  <InputNumber
                    style={{ width: '50%' }}
                    value={r.virtual_line.y1}
                    onChange={(v) => patchRow(r.key, { virtual_line: { ...r.virtual_line, y1: v ?? 0 } })}
                  />
                </Space.Compact>
              </Form.Item>
            </Col>
            <Col span={6}>
              <Form.Item label="虚拟线 终点 x,y">
                <Space.Compact style={{ width: '100%' }}>
                  <InputNumber
                    style={{ width: '50%' }}
                    value={r.virtual_line.x2}
                    onChange={(v) => patchRow(r.key, { virtual_line: { ...r.virtual_line, x2: v ?? 0 } })}
                  />
                  <InputNumber
                    style={{ width: '50%' }}
                    value={r.virtual_line.y2}
                    onChange={(v) => patchRow(r.key, { virtual_line: { ...r.virtual_line, y2: v ?? 0 } })}
                  />
                </Space.Compact>
              </Form.Item>
            </Col>
          </Row>
        </Card>
      ))}
      <Space>
        <Button type="primary" disabled={dirty || rows.length === 0} loading={saving} onClick={() => save(false)}>
          保存
        </Button>
        <Button disabled={dirty || rows.length === 0} loading={apply} onClick={() => { setApply(true); save(true).finally(() => setApply(false)) }}>
          保存并应用（重建流水线）
        </Button>
      </Space>

      <Modal
        open={!!previewCam}
        title={`画面预览 · ${previewCam?.camera_id || ''}`}
        footer={null}
        onCancel={closePreview}
        width={560}
      >
        <div style={{ textAlign: 'center', minHeight: 200, padding: '8px 0' }}>
          {frame ? (
            <img
              src={`data:${frame.mime};base64,${frame.b64}`}
              alt="preview"
              style={{ maxWidth: '100%', maxHeight: 380, borderRadius: 6, background: '#000' }}
            />
          ) : (
            <Empty
              description={previewErr ? '暂无画面（摄像头未打开或未采集到帧）' : '加载中…'}
              style={{ paddingTop: 48 }}
            />
          )}
        </div>
        <div style={{ textAlign: 'center', color: '#999', fontSize: 12 }}>
          约 1.5 秒刷新一帧；预览来自边缘盒本机，不占用后台带宽
        </div>
      </Modal>
    </Card>
  )
}