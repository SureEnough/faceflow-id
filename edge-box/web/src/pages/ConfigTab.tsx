import { useEffect, useState } from 'react'
import {
  Button,
  Card,
  Col,
  Form,
  Input,
  InputNumber,
  Row,
  Switch,
  message,
} from 'antd'
import { fetchConfig, saveConfig } from '../api'
import type { EdgeBoxConfig } from '../types'

export default function ConfigTab({ onSaved }: { onSaved: () => void }) {
  const [form] = Form.useForm()
  const [loading, setLoading] = useState(false)
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    setLoading(true)
    fetchConfig()
      .then((cfg) => form.setFieldsValue(cfg))
      .catch(() => message.error('配置获取失败'))
      .finally(() => setLoading(false))
  }, [form])

  const onFinish = async (values: Partial<EdgeBoxConfig>) => {
    setSaving(true)
    try {
      // 密码/PSK 留空表示不修改（空格清掉）
      const payload: Partial<EdgeBoxConfig> = { ...values }
      if (!payload.device_psk) delete payload.device_psk
      if (!payload.web_password) delete payload.web_password
      await saveConfig(payload)
      message.success('已保存')
      onSaved()
    } catch (e) {
      message.error((e as Error).message || '保存失败')
    } finally {
      setSaving(false)
    }
  }

  return (
    <Card size="small" title="全局参数">
      <Form<Partial<EdgeBoxConfig>>
        form={form}
        layout="vertical"
        onFinish={onFinish}
        style={{ maxWidth: 760 }}
        disabled={loading}
      >
        <Row gutter={16}>
          <Col span={8}>
            <Form.Item label="设备 ID" name="device_id">
              <InputNumber style={{ width: '100%' }} min={0} step={1} />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="上报端点" name="report_endpoint">
              <Input placeholder="http://host:port/api/v1" />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="上报间隔（秒）" name="report_interval_s">
              <InputNumber style={{ width: '100%' }} min={1} step={1} />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="记录保留天数" name="retention_days">
              <InputNumber style={{ width: '100%' }} min={1} step={1} />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="检测阈值" name="det_thresh">
              <InputNumber style={{ width: '100%' }} min={0} max={1} step={0.01} />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="1:N 识别阈值" name="recog_thresh">
              <InputNumber style={{ width: '100%' }} min={0} max={1} step={0.01} />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="活体检测" name="liveness_enabled" valuePropName="checked">
              <Switch />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="内部人员识别" name="staff_enabled" valuePropName="checked">
              <Switch />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="设备 PSK（留空保持不变）" name="device_psk">
              <Input.Password placeholder="********" />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="Web 端口" name="web_port">
              <InputNumber style={{ width: '100%' }} min={1} max={65535} step={1} />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="Web 账号" name="web_username">
              <Input />
            </Form.Item>
          </Col>
          <Col span={8}>
            <Form.Item label="Web 密码（留空保持不变）" name="web_password">
              <Input.Password placeholder="********" />
            </Form.Item>
          </Col>
        </Row>
        <Button type="primary" htmlType="submit" loading={saving}>
          保存参数
        </Button>
      </Form>
    </Card>
  )
}