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
  Table,
  Tag,
  message,
} from 'antd'
import type { ColumnsType } from 'antd/es/table'
import { DeleteOutlined, EditOutlined, EyeOutlined, PlusOutlined } from '@ant-design/icons'
import { fetchConfig, fetchPreview, saveConfig, triggerReload } from '../api'
import type { PreviewFrame } from '../api'
import type { CameraCfg } from '../types'

interface InstrumentedCam extends CameraCfg {
  key: number
}

const DIR_TEXT: Record<string, string> = { both: '双向', in: '进', out: '出' }

function CameraFormFields() {
  return (
    <>
      <Form.Item
        name="camera_id" label="摄像头 ID" rules={[{ required: true, message: '请输入摄像头 ID' }]}
      >
        <Input placeholder="cam-01" />
      </Form.Item>
      <Form.Item
        name="url" label="URL（rtsp:// 或 usb0）" rules={[{ required: true, message: '请输入 URL' }]}
      >
        <Input placeholder="rtsp://192.168.1.20/stream1" />
      </Form.Item>
      <Row gutter={12}>
        <Col span={12}>
          <Form.Item name="count_flow" label="计入客流" valuePropName="checked" initialValue={true}>
            <Switch />
          </Form.Item>
        </Col>
        <Col span={12}>
          <Form.Item name="direction" label="判向" initialValue="both">
            <Select
              options={[
                { value: 'both', label: '双向' },
                { value: 'in', label: '只进' },
                { value: 'out', label: '只出' },
              ]}
            />
          </Form.Item>
        </Col>
      </Row>
      <Form.Item label="虚拟线（客流计数，可选）" style={{ marginBottom: 4 }}>
        <Row gutter={8}>
          <Col span={6}>
            <Form.Item name={['virtual_line', 'x1']} noStyle>
              <InputNumber placeholder="x1" style={{ width: '100%' }} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item name={['virtual_line', 'y1']} noStyle>
              <InputNumber placeholder="y1" style={{ width: '100%' }} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item name={['virtual_line', 'x2']} noStyle>
              <InputNumber placeholder="x2" style={{ width: '100%' }} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item name={['virtual_line', 'y2']} noStyle>
              <InputNumber placeholder="y2" style={{ width: '100%' }} />
            </Form.Item>
          </Col>
        </Row>
      </Form.Item>
    </>
  )
}

export default function CamerasTab({ onSaved }: { onSaved: () => void }) {
  const [rows, setRows] = useState<InstrumentedCam[]>([])
  const [loading, setLoading] = useState(false)
  const [saving, setSaving] = useState(false)
  const [apply, setApply] = useState(false)
  const [modalOpen, setModalOpen] = useState(false)
  const [editing, setEditing] = useState<InstrumentedCam | null>(null)
  const [form] = Form.useForm()

  // ---- 画面预览 ----
  const [previewCam, setPreviewCam] = useState<InstrumentedCam | null>(null)
  const [frame, setFrame] = useState<PreviewFrame | null>(null)
  const [previewErr, setPreviewErr] = useState<string | null>(null)

  useEffect(() => {
    setLoading(true)
    fetchConfig()
      .then((cfg) => setRows(cfg.cameras.map((c, i) => ({ ...c, key: i }))))
      .catch(() => message.error('配置获取失败'))
      .finally(() => setLoading(false))
  }, [])

  const dirty = useMemo(() => rows.some((r) => !r.camera_id || !r.url), [rows])

  const openAdd = () => {
    setEditing(null)
    form.resetFields()
    form.setFieldsValue({ count_flow: true, direction: 'both', virtual_line: { x1: 0, y1: 0, x2: 0, y2: 0 } })
    setModalOpen(true)
  }
  const openEdit = (r: InstrumentedCam) => {
    setEditing(r)
    form.setFieldsValue(r)
    setModalOpen(true)
  }
  const closeModal = () => {
    setModalOpen(false)
    setEditing(null)
  }

  const submit = async (values: InstrumentedCam) => {
    if (editing) {
      setRows(rows.map((r) => (r.key === editing.key ? { ...r, ...values, key: r.key } : r)))
    } else {
      setRows([...rows, { ...values, key: Date.now() }])
    }
    setModalOpen(false)
    setEditing(null)
  }

  const removeRow = (key: number) => setRows(rows.filter((r) => r.key !== key))

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

  // ---- 预览轮询 ----
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

  const columns: ColumnsType<InstrumentedCam> = [
    { title: 'ID', dataIndex: 'camera_id' },
    { title: 'URL', dataIndex: 'url', ellipsis: true },
    {
      title: '计入客流', dataIndex: 'count_flow', width: 90,
      render: (v: boolean) => (v ? <Tag color="green">是</Tag> : <Tag>否</Tag>),
    },
    {
      title: '判向', dataIndex: 'direction', width: 80,
      render: (v: string) => DIR_TEXT[v] ?? v,
    },
    {
      title: '虚拟线', width: 180,
      render: (_, r) => (
        <span style={{ fontFamily: 'monospace', fontSize: 12 }}>
          ({r.virtual_line.x1}, {r.virtual_line.y1}) → ({r.virtual_line.x2}, {r.virtual_line.y2})
        </span>
      ),
    },
    {
      title: '操作', width: 170,
      render: (_, r) => (
        <Space size={4}>
          <Button size="small" icon={<EyeOutlined />} disabled={!r.camera_id}
                  onClick={() => { setFrame(null); setPreviewErr(null); setPreviewCam(r) }}>
            预览
          </Button>
          <Button size="small" icon={<EditOutlined />} onClick={() => openEdit(r)}>编辑</Button>
          <Popconfirm title="确认删除该摄像头？" onConfirm={() => removeRow(r.key)}>
            <Button size="small" danger icon={<DeleteOutlined />} />
          </Popconfirm>
        </Space>
      ),
    },
  ]

  return (
    <Card
      size="small"
      title="摄像头管理"
      loading={loading}
      extra={
        <Button type="primary" icon={<PlusOutlined />} onClick={openAdd}>
          添加摄像头
        </Button>
      }
    >
      <Table<InstrumentedCam>
        rowKey="key"
        size="small"
        dataSource={rows}
        columns={columns}
        pagination={false}
        locale={{ emptyText: <Empty description="暂无摄像头，点击右上角“添加摄像头”" /> }}
      />
      <div style={{ marginTop: 12, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <span style={{ color: '#999', fontSize: 12 }}>
          {dirty ? '请填写完整 camera_id 与 URL 后再保存。' : '保存后需点“保存并应用”使摄像头热生效。'}
        </span>
        <Space>
          <Button disabled={dirty || rows.length === 0} loading={saving} onClick={() => save(false)}>
            保存
          </Button>
          <Button type="primary" disabled={dirty || rows.length === 0} loading={apply}
                  onClick={() => { setApply(true); save(true).finally(() => setApply(false)) }}>
            保存并应用（重建流水线）
          </Button>
        </Space>
      </div>

      {/* 添加 / 编辑弹窗 */}
      <Modal
        title={editing ? `编辑摄像头 ${editing.camera_id}` : '添加摄像头'}
        open={modalOpen}
        onOk={() => form.submit()}
        onCancel={closeModal}
        destroyOnClose
        width={520}
      >
        <Form<InstrumentedCam> form={form} layout="vertical" onFinish={submit}>
          <CameraFormFields />
        </Form>
      </Modal>

      {/* 画面预览弹窗 */}
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
            <Empty description={previewErr ? '暂无画面（摄像头未打开或未采集到帧）' : '加载中…'} style={{ paddingTop: 48 }} />
          )}
        </div>
        <div style={{ textAlign: 'center', color: '#999', fontSize: 12 }}>
          约 1.5 秒刷新一帧；预览来自边缘盒本机，不占用后台带宽
        </div>
      </Modal>
    </Card>
  )
}