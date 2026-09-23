import { useEffect, useMemo, useState } from 'react'
import { Alert, Button, Card, Form, Input, Modal, Select, Space, Tag, Tree, Typography, Spin, message } from 'antd'
import type { TreeDataNode } from 'antd'
import { SettingOutlined, EditOutlined } from '@ant-design/icons'
import dayjs from 'dayjs'
import { fetchDeviceConfig, fetchDeviceTree, pushDeviceConfig, updateDevice } from '../api'
import { canWrite } from '../utils/role'
import { DEVICE_TYPE_TEXT, type Device } from '../api/types'

function flatten(items: Device[], depth = 0): { id: number; key: string; device_type: number }[] {
  const out: { id: number; key: string; device_type: number }[] = []
  for (const d of items) {
    out.push({ id: d.id, key: `${'　'.repeat(depth)}${d.name} (#${d.id})`, device_type: d.device_type })
    if (d.children?.length) out.push(...flatten(d.children, depth + 1))
  }
  return out
}

function toTree(items: Device[]): TreeDataNode[] {
  return items.map((d) => ({
    key: d.id,
    title: (
      <Space size={8} wrap>
        <Tag color={DEVICE_TYPE_TEXT[d.device_type as keyof typeof DEVICE_TYPE_TEXT] ? 'blue' : 'default'}>
          {DEVICE_TYPE_TEXT[d.device_type as keyof typeof DEVICE_TYPE_TEXT] ?? '设备'}
        </Tag>
        <span>{d.name}</span>
        <Typography.Text type="secondary">{d.id}</Typography.Text>
        {d.device_key && <Typography.Text type="secondary">{d.device_key}</Typography.Text>}
        <Tag color={d.status === 1 ? 'green' : 'red'}>{d.status === 1 ? '在线' : '离线'}</Tag>
        {d.last_seen_at > 0 && (
          <Typography.Text type="secondary">
            最后在线 {dayjs.unix(d.last_seen_at).format('MM-DD HH:mm:ss')}
          </Typography.Text>
        )}
        {(d.cpu || d.mem || d.fps) ? (
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            CPU {Math.round((d.cpu ?? 0))}% · 内存 {Math.round((d.mem ?? 0))}% · 磁盘 {Math.round((d.disk ?? 0))}% · {Math.round((d.fps ?? 0))} FPS
          </Typography.Text>
        ) : null}
      </Space>
    ),
    children: d.children?.length ? toTree(d.children) : undefined,
  }))
}

export default function Devices() {
  const [tree, setTree] = useState<TreeDataNode[]>([])
  const [options, setOptions] = useState<{ id: number; key: string; device_type: number }[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')

  // 配置下发弹窗
  const [pushOpen, setPushOpen] = useState(false)
  const [pushLoading, setPushLoading] = useState(false)
  const [pushForm] = Form.useForm<{ device_id: number; config: string }>()
  // 编辑名称弹窗
  const [renameOpen, setRenameOpen] = useState(false)
  const [renameLoading, setRenameLoading] = useState(false)
  const [renameForm] = Form.useForm<{ device_id: number; name: string }>()

  const loadTree = () =>
    fetchDeviceTree()
      .then((items) => {
        setTree(toTree(items))
        setOptions(flatten(items).filter((d) => [1, 2].includes(d.device_type))) // 仅主设备
      })
      .catch((e) => setError(String(e?.message ?? e)))
      .finally(() => setLoading(false))

  useEffect(() => {
    loadTree()
  }, [])

  const deviceOptions = useMemo(
    () => options.map((o) => ({ value: o.id, label: o.key })),
    [options],
  )

  const onSelectDevice = async (id: number) => {
    setPushLoading(true)
    try {
      const { config } = await fetchDeviceConfig(id)
      pushForm.setFieldsValue({ device_id: id, config: JSON.stringify(config ?? {}, null, 2) })
    } catch (e) {
      message.error(String((e as Error).message ?? e))
    } finally {
      setPushLoading(false)
    }
  }

  const onPush = async () => {
    const values = await pushForm.validateFields()
    let parsed: Record<string, unknown>
    try {
      parsed = JSON.parse(values.config)
    } catch {
      message.error('配置不是合法 JSON')
      return
    }
    setPushLoading(true)
    try {
      await pushDeviceConfig(values.device_id, parsed)
      message.success('已下发，设备将在轮询周期内热重载')
      setPushOpen(false)
    } catch (e) {
      message.error(String((e as Error).message ?? e))
    } finally {
      setPushLoading(false)
    }
  }

  const onRename = async () => {
    const values = await renameForm.validateFields()
    setRenameLoading(true)
    try {
      await updateDevice(values.device_id, { name: values.name })
      message.success('已改名')
      setRenameOpen(false)
      renameForm.resetFields()
      loadTree()
    } catch (e) {
      message.error(String((e as Error).message ?? e))
    } finally {
      setRenameLoading(false)
    }
  }

  return (
    <Card
      title="设备树（门店 → 主设备 → 子设备）"
      extra={
        <Space>
          {canWrite() && <Button icon={<EditOutlined />} onClick={() => setRenameOpen(true)}>编辑名称</Button>}
          {canWrite() && <Button icon={<SettingOutlined />} onClick={() => setPushOpen(true)}>配置下发</Button>}
        </Space>
      }
    >
      {error && <Alert type="error" showIcon message={error} style={{ marginBottom: 12 }} />}
      {loading ? (
        <Spin />
      ) : (
        <Tree showLine defaultExpandAll treeData={tree} />
      )}

      <Modal
        title="向主设备下发配置（远程配置优先，门店边缘盒轮询后自动应用）"
        open={pushOpen}
        onCancel={() => setPushOpen(false)}
        onOk={onPush}
        confirmLoading={pushLoading}
        width={720}
        okText="下发"
        cancelText="取消"
      >
        <Form form={pushForm} layout="vertical">
          <Form.Item name="device_id" label="主设备" rules={[{ required: true, message: '请选择设备' }]}>
            <Select
              showSearch
              placeholder="选择边缘盒 / 录入电脑端"
              options={deviceOptions}
              onSelect={onSelectDevice}
              loading={pushLoading}
            />
          </Form.Item>
          <Form.Item
            name="config"
            label="配置 JSON（cameras / 阈值 / report_endpoint 等；设备身份与 Web 安全字段以本地为准）"
            rules={[{ required: true, message: '请输入配置 JSON' }]}
          >
            <Input.TextArea rows={12} style={{ fontFamily: 'monospace' }} placeholder='{\n  "det_thresh": 0.5,\n  "cameras": [...]\n}' />
          </Form.Item>
        </Form>
      </Modal>

      <Modal
        title="编辑设备名称"
        open={renameOpen}
        onCancel={() => setRenameOpen(false)}
        onOk={onRename}
        confirmLoading={renameLoading}
        okText="保存"
        cancelText="取消"
      >
        <Form form={renameForm} layout="vertical">
          <Form.Item name="device_id" label="设备" rules={[{ required: true, message: '请选择设备' }]}>
            <Select showSearch placeholder="选择要改名的设备（含子设备）" options={deviceOptions} />
          </Form.Item>
          <Form.Item name="name" label="新名称" rules={[{ required: true, message: '请输入新名称' }]}>
            <Input placeholder="例如：东门边缘盒" maxLength={64} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}