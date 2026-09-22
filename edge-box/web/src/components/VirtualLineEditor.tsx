import { useCallback, useEffect, useRef, useState } from 'react'

// 虚拟线（原始视频分辨率坐标）
export interface VirtualLine {
  x1: number
  y1: number
  x2: number
  y2: number
}

// 在预览画面中配置虚拟线：图片上叠加 SVG，两端圆点可拖拽。
// 显示坐标 → 原始分辨率坐标按 srcWidth/srcHeight 映射（预览图已降采样）。
interface Props {
  dataUrl: string
  srcWidth: number
  srcHeight: number
  line: VirtualLine
  onChange: (line: VirtualLine) => void
  disabled?: boolean
}

export default function VirtualLineEditor({ dataUrl, srcWidth, srcHeight, line, onChange, disabled }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null)
  const [size, setSize] = useState({ w: 0, h: 0 })
  const [drag, setDrag] = useState<0 | 1 | 2>(0)

  const measure = useCallback(() => {
    const el = wrapRef.current
    if (el) setSize({ w: el.clientWidth, h: el.clientHeight })
  }, [])

  useEffect(() => {
    measure()
    window.addEventListener('resize', measure)
    return () => window.removeEventListener('resize', measure)
  }, [measure])

  // 显示空间像素 → 原始分辨率坐标
  const toSrc = (px: number, src: number, disp: number) =>
    disp > 0 ? Math.round((px / disp) * src) : 0
  // 原始坐标 → 显示空间像素
  const toPx = (v: number, src: number, disp: number) =>
    src > 0 ? (v / src) * disp : 0

  const onPointerMove = (e: React.PointerEvent) => {
    if (!drag || disabled || !wrapRef.current) return
    const rect = wrapRef.current.getBoundingClientRect()
    const x = toSrc(e.clientX - rect.left, srcWidth, rect.width)
    const y = toSrc(e.clientY - rect.top, srcHeight, rect.height)
    const nx = Math.max(0, Math.min(srcWidth, x))
    const ny = Math.max(0, Math.min(srcHeight, y))
    onChange(drag === 1 ? { ...line, x1: nx, y1: ny } : { ...line, x2: nx, y2: ny })
  }

  const ends = [
    { idx: 1 as const, cx: toPx(line.x1, srcWidth, size.w), cy: toPx(line.y1, srcHeight, size.h) },
    { idx: 2 as const, cx: toPx(line.x2, srcWidth, size.w), cy: toPx(line.y2, srcHeight, size.h) },
  ]

  return (
    <div
      ref={wrapRef}
      style={{ position: 'relative', width: '100%', userSelect: 'none', cursor: disabled ? 'default' : 'crosshair' }}
      onPointerMove={onPointerMove}
      onPointerUp={() => setDrag(0)}
      onPointerLeave={() => setDrag(0)}
    >
      <img
        src={dataUrl}
        alt="preview"
        draggable={false}
        onLoad={measure}
        style={{ display: 'block', width: '100%', borderRadius: 6, pointerEvents: 'none' }}
      />
      {size.w > 0 && (
        <svg
          viewBox={`0 0 ${size.w} ${size.h}`}
          style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', overflow: 'visible' }}
        >
          <line
            x1={ends[0].cx} y1={ends[0].cy} x2={ends[1].cx} y2={ends[1].cy}
            stroke="#f5222d" strokeWidth={2} strokeDasharray="6 4"
          />
          {ends.map((p) => (
            <circle
              key={p.idx}
              cx={p.cx} cy={p.cy} r={8}
              fill="rgba(255,255,255,0.9)" stroke="#f5222d" strokeWidth={2}
              style={{ cursor: disabled ? 'default' : 'grab', touchAction: 'none' }}
              onPointerDown={(e) => { e.preventDefault(); if (!disabled) setDrag(p.idx) }}
            />
          ))}
        </svg>
      )}
    </div>
  )
}