import { useEffect, useState } from 'react'
import { Button, Card, Form, Input, Space, Tag, Typography, message } from 'antd'
import { fetchSystemConfig, saveSystemConfig } from '../api'
import { errMsg } from '../api/client'
import type { SystemConfig } from '../api/types'

// 全局系统配置：人脸识别服务（face-service / 边缘盒子人脸识别端）
export default function SystemConfigPage() {
  const [form] = Form.useForm()
  const [loading, setLoading] = useState(false)
  const [saving, setSaving] = useState(false)
  const [cfg, setCfg] = useState<SystemConfig | null>(null)

  const load = () => {
    setLoading(true)
    fetchSystemConfig()
      .then((r) => {
        setCfg(r)
        form.setFieldsValue({ face_service_url: r.face_service_url, face_service_key: '' })
      })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, []) // eslint-disable-line react-hooks/exhaustive-deps

  const submit = async () => {
    const values = await form.validateFields()
    const body: { face_service_url?: string; face_service_key?: string } = {
      face_service_url: (values.face_service_url ?? '').trim(),
    }
    // 密钥输入框有值时提交；注意后端约定空串/*** 表示不修改
    if (values.face_service_key) body.face_service_key = values.face_service_key
    setSaving(true)
    try {
      await saveSystemConfig(body)
      message.success('已保存')
      form.setFieldsValue({ face_service_key: '' })
      load()
    } catch (e) {
      message.error(errMsg(e))
    } finally {
      setSaving(false)
    }
  }

  return (
    <Card title="系统配置" loading={loading}>
      <Typography.Paragraph type="secondary">
        配置人脸识别服务（face-service / 边缘盒子人脸识别端）的接口地址与密钥。
        人员管理中手动添加 / 模板导入人员时，后台将调用该服务对头像做
        人脸识别（提取 512 维特征）。
      </Typography.Paragraph>

      {cfg && (
        <Space wrap style={{ marginBottom: 16 }}>
          <Tag color={cfg.effective_key_used ? 'green' : 'orange'}>
            当前生效地址：{cfg.effective_url}（密钥{cfg.effective_key_used ? '已配置' : '未配置'}）
          </Tag>
          <Tag>默认地址：{cfg.face_service_default}</Tag>
        </Space>
      )}

      <Form form={form} layout="vertical" style={{ maxWidth: 560 }}>
        <Form.Item
          name="face_service_url"
          label="人脸识别服务地址"
          extra="留空则使用默认 face-service（http://127.0.0.1:8090，可用环境变量 FACE_SERVICE_URL 覆写默认值）"
        >
          <Input placeholder="http://127.0.0.1:8090" allowClear />
        </Form.Item>
        <Form.Item
          name="face_service_key"
          label="人脸识别服务密钥"
          extra={cfg?.face_service_key_set ? '已配置密钥；如需修改请输入新密钥，留空保持不变' : '未配置密钥；设置后请求将携带 X-API-Key'}
        >
          <Input.Password placeholder="留空表示不修改" allowClear />
        </Form.Item>
        <Form.Item>
          <Button type="primary" loading={saving} onClick={submit}>保存</Button>
        </Form.Item>
      </Form>
    </Card>
  )
}