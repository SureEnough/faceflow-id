import { useCallback, useEffect, useRef, useState } from 'react'
import { Modal, Slider, Space, Button, Typography, message } from 'antd'
import Cropper from 'react-easy-crop'
import type { Area } from 'react-easy-crop'

interface Props {
  src: string // 待裁剪原图 dataURL（带前缀）
  open: boolean
  aspect?: number // 裁剪宽高比，默认 1（正方形，适合人脸头像）
  outputSize?: number // 输出边长（像素），默认 512
  onCancel: () => void
  onConfirm: (base64: string, dataUrl: string) => void
}

// 按裁剪区域重绘到指定正方形尺寸，返回 base64（无前缀）+ dataURL（带前缀）
function cropImage(src: string, pixelCrop: Area, outputSize: number): Promise<{ base64: string; dataUrl: string }> {
  return new Promise((resolve, reject) => {
    const image = new Image()
    image.onload = () => {
      const canvas = document.createElement('canvas')
      canvas.width = outputSize
      canvas.height = outputSize
      const ctx = canvas.getContext('2d')
      if (!ctx) {
        reject(new Error('canvas not supported'))
        return
      }
      ctx.fillStyle = '#fff'
      ctx.fillRect(0, 0, outputSize, outputSize)
      ctx.drawImage(
        image,
        pixelCrop.x, pixelCrop.y, pixelCrop.width, pixelCrop.height,
        0, 0, outputSize, outputSize,
      )
      const dataUrl = canvas.toDataURL('image/jpeg', 0.95)
      resolve({ base64: dataUrl.split(',')[1] ?? '', dataUrl })
    }
    image.onerror = () => reject(new Error('image load failed'))
    image.src = src
  })
}

export default function AvatarCropper({
  src, open, aspect = 1, outputSize = 512, onCancel, onConfirm,
}: Props) {
  const [crop, setCrop] = useState({ x: 0, y: 0 })
  const [zoom, setZoom] = useState(1)
  const [rotation, setRotation] = useState(0)
  const [croppedAreaPixels, setCroppedAreaPixels] = useState<Area | null>(null)
  const [saving, setSaving] = useState(false)
  // 每次打开重置
  const first = useRef(true)
  useEffect(() => {
    if (open && (first.current || true)) {
      setCrop({ x: 0, y: 0 })
      setZoom(1)
      setRotation(0)
      setCroppedAreaPixels(null)
      first.current = false
    }
  }, [open])

  const onCropComplete = useCallback((_area: Area, areaPixels: Area) => {
    setCroppedAreaPixels(areaPixels)
  }, [])

  const confirm = async () => {
    if (!src || !croppedAreaPixels) return
    setSaving(true)
    try {
      const { base64, dataUrl } = await cropImage(src, croppedAreaPixels, outputSize)
      onConfirm(base64, dataUrl)
    } catch (e) {
      message.error('裁剪失败：' + String(e))
    } finally {
      setSaving(false)
    }
  }

  return (
    <Modal
      title="裁剪头像"
      open={open}
      onCancel={onCancel}
      width={560}
      footer={
        <Space>
          <Button onClick={onCancel}>取消</Button>
          <Button type="primary" loading={saving} onClick={confirm}>确认裁剪</Button>
        </Space>
      }
      destroyOnClose
    >
      <div style={{ position: 'relative', height: 320, background: '#f5f5f5', borderRadius: 8, overflow: 'hidden' }}>
        {src && (
          <Cropper
            image={src}
            crop={crop}
            zoom={zoom}
            rotation={rotation}
            aspect={aspect}
            onCropChange={setCrop}
            onZoomChange={setZoom}
            onRotationChange={setRotation}
            onCropComplete={onCropComplete}
          />
        )}
      </div>
      <div style={{ marginTop: 16 }}>
        <Typography.Text type="secondary">缩放</Typography.Text>
        <Slider min={1} max={3} step={0.01} value={zoom} onChange={setZoom} />
        <Typography.Text type="secondary">旋转</Typography.Text>
        <Slider min={-180} max={180} step={1} value={rotation} onChange={setRotation} />
      </div>
    </Modal>
  )
}