import { useEffect, useState } from 'react'
import { Badge, Card, Space, Tag, Tree, Typography, Spin, Alert } from 'antd'
import type { TreeDataNode } from 'antd'
import { fetchDeviceTree } from '../api'
import { DEVICE_TYPE_TEXT, type Device } from '../api/types'

function toTree(items: Device[]): TreeDataNode[] {
  return items.map((d) => ({
    key: d.id,
    title: (
      <Space size={8}>
        <Tag color={DEVICE_TYPE_TEXT[d.device_type as keyof typeof DEVICE_TYPE_TEXT] ? 'blue' : 'default'}>
          {DEVICE_TYPE_TEXT[d.device_type as keyof typeof DEVICE_TYPE_TEXT] ?? '设备'}
        </Tag>
        <span>{d.name}</span>
        <Badge status={d.status === 1 ? 'success' : 'error'} text={d.status === 1 ? '在线' : '离线'} />
        {d.device_key && <Typography.Text type="secondary">{d.device_key}</Typography.Text>}
      </Space>
    ),
    children: d.children?.length ? toTree(d.children) : undefined,
  }))
}

export default function Devices() {
  const [tree, setTree] = useState<TreeDataNode[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')

  useEffect(() => {
    fetchDeviceTree()
      .then((items) => setTree(toTree(items)))
      .catch((e) => setError(String(e?.message ?? e)))
      .finally(() => setLoading(false))
  }, [])

  return (
    <Card title="设备树（门店 → 主设备 → 子设备）">
      {error && <Alert type="error" showIcon message={error} style={{ marginBottom: 12 }} />}
      {loading ? (
        <Spin />
      ) : (
        <Tree showLine defaultExpandAll treeData={tree} />
      )}
    </Card>
  )
}