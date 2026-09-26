// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

// dsh patch：头像裁剪对话框
//
// 背景：Mumble 原来的「更换头像」是把文件原样发出去，于是
//   ① 宽或高 > 1024 的图会被客户端静默丢弃（没有任何提示）；
//   ② 编码后超过服务端 image_message_length（默认 1 MB）会被服务端 PERM_DENIED(TextTooLong) 拒收；
//   ③ 用户无法调整构图（不能缩放/平移/裁成正方形）。
// 这个对话框补上裁剪/缩放 UI，配合 MainWindow 侧的编码逻辑共同解决上面三点。

#ifndef MUMBLE_MUMBLE_AVATARCROPDIALOG_H_
#define MUMBLE_MUMBLE_AVATARCROPDIALOG_H_

#include <QtCore/QPointF>
#include <QtGui/QImage>
#include <QtWidgets/QDialog>

class QLabel;
class QSlider;

class AvatarCropDialog : public QDialog {
private:
	Q_OBJECT
	Q_DISABLE_COPY(AvatarCropDialog)

public:
	explicit AvatarCropDialog(const QImage &source, QWidget *parent = nullptr);

	/// 输出裁剪后的正方形头像（边长 side 像素，平滑缩放）
	QImage croppedImage(int side = 512) const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void onZoomSliderChanged(int value);
	void resetView();

private:
	QRect cropRect() const;
	QRect paintArea() const;
	qreal effectiveZoom() const;
	void updateBaseZoom();
	void clampOffset();
	void updateHint();

	QImage m_source;
	/// 让图片刚好铺满裁剪框的缩放（相对原图）= 滑条 100% 的基准
	qreal m_baseZoom = 1.0;
	/// 用户相对基准的缩放倍数；下限 m_minFactor，上限 4.0
	qreal m_zoomFactor = 1.0;
	/// 缩放倍数下限 = 「整图刚好放进裁剪框」/「铺满裁剪框」（0 < 值 <= 1）。
	/// 旧版把下限写死为 1.0（= 只能放大不能缩小），大头照永远框不住整颗头。
	qreal m_minFactor = 1.0;
	/// 图片中心相对裁剪框中心的偏移（组件像素）
	QPointF m_offset;
	QPointF m_dragOrigin;
	bool m_dragging = false;

	QSlider *m_zoomSlider = nullptr;
	QLabel *m_hint        = nullptr;
};

#endif
