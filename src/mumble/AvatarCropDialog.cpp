// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AvatarCropDialog.h"

#include <cmath>

#include <QtCore/QBuffer>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>

AvatarCropDialog::AvatarCropDialog(const QImage &source, QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("Crop avatar"));
	m_source = source.convertToFormat(QImage::Format_ARGB32);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->addStretch(1); // 上方留给自绘的预览区

	m_hint = new QLabel(this);
	m_hint->setAlignment(Qt::AlignCenter);
	layout->addWidget(m_hint);

	m_zoomSlider = new QSlider(Qt::Horizontal, this);
	m_zoomSlider->setRange(100, 400);
	m_zoomSlider->setValue(100);
	m_zoomSlider->setToolTip(tr("Zoom: 100% fills the square, drag left to zoom out and fit the whole image"));
	layout->addWidget(m_zoomSlider);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	QPushButton *resetButton  = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
	if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok)) {
		okButton->setText(tr("Use this crop"));
	}
	layout->addWidget(buttons);

	connect(m_zoomSlider, &QSlider::valueChanged, this, &AvatarCropDialog::onZoomSliderChanged);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(resetButton, &QPushButton::clicked, this, &AvatarCropDialog::resetView);

	resize(460, 540);
	updateBaseZoom();
	clampOffset();
	updateHint();
}

QRect AvatarCropDialog::paintArea() const {
	QRect area = rect();
	if (m_zoomSlider) {
		area.setBottom(qMin(area.bottom(), m_zoomSlider->geometry().top() - 8));
	}
	return area;
}

QRect AvatarCropDialog::cropRect() const {
	const QRect area = paintArea();
	const int side   = qMax(80, static_cast< int >(qMin(area.width(), area.height()) * 0.78));
	return QRect(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
}

qreal AvatarCropDialog::effectiveZoom() const {
	return m_baseZoom * m_zoomFactor;
}

void AvatarCropDialog::updateBaseZoom() {
	const QRect crop   = cropRect();
	qreal cover        = 1.0;
	qreal contain      = 1.0;
	if ((m_source.width() > 0) && (m_source.height() > 0) && (crop.width() > 0)) {
		cover   = qMax(static_cast< qreal >(crop.width()) / m_source.width(),
					 static_cast< qreal >(crop.height()) / m_source.height());
		contain = qMin(static_cast< qreal >(crop.width()) / m_source.width(),
					   static_cast< qreal >(crop.height()) / m_source.height());
	}
	// 100% = 刚好铺满裁剪框（构图最紧凑）
	m_baseZoom = qMax(1.0, cover);

	// 相对基准能缩到的最小倍数：缩到这里整张图刚好装进裁剪框（露出底色，但能框住整颗头）。
	m_minFactor = 1.0;
	if (cover > 0.0) {
		m_minFactor = qBound(0.05, contain / cover, 1.0);
	}

	if (m_zoomSlider) {
		// 向下取整到 5 的倍数，保证「整图可见」这一档一定够得着
		int minPercent = static_cast< int >(std::floor(m_minFactor * 100.0 / 5.0)) * 5;
		minPercent     = qBound(5, minPercent, 100);
		// setRange 会把越界的当前值夹回范围内（值变化会触发 valueChanged，m_zoomFactor 自动跟上）
		m_zoomSlider->setRange(minPercent, 400);
	}
}

void AvatarCropDialog::clampOffset() {
	const QRect crop = cropRect();
	const qreal zoom = effectiveZoom();
	const qreal maxX = qMax(0.0, (m_source.width() * zoom - crop.width()) / 2.0);
	const qreal maxY = qMax(0.0, (m_source.height() * zoom - crop.height()) / 2.0);
	m_offset.setX(qBound(-maxX, m_offset.x(), maxX));
	m_offset.setY(qBound(-maxY, m_offset.y(), maxY));
}

void AvatarCropDialog::updateHint() {
	if (!m_hint) {
		return;
	}
	const qreal zoom = effectiveZoom();
	const int minPct = m_zoomSlider ? m_zoomSlider->minimum() : 100;
	m_hint->setText(tr("Output 512x512  ·  source %1x%2  ·  zoom %3% (range %4% - 400%; 100% fills the square)")
						.arg(m_source.width())
						.arg(m_source.height())
						.arg(qRound(zoom * 100))
						.arg(minPct));
	m_hint->setToolTip(tr("Drag inside the square to move the image; use the slider or the mouse wheel to zoom.\n"
						  "100% fills the square; slide left all the way to fit the whole image in.\n"
						  "The square is what will be sent as your avatar."));
}

void AvatarCropDialog::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.fillRect(rect(), QColor(28, 28, 28));

	const QRect crop = cropRect();
	const qreal zoom = effectiveZoom();
	const QSize dispSize(qRound(m_source.width() * zoom), qRound(m_source.height() * zoom));
	const QPoint topLeft(qRound(crop.center().x() + m_offset.x() - dispSize.width() / 2.0),
						 qRound(crop.center().y() + m_offset.y() - dispSize.height() / 2.0));

	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	// 先给裁剪框铺一层比底色亮的「空隙色」：缩小到整图可见时，图片外的部分就是这块颜色，
	// 用户一眼能看出「图的边界在哪、框住的是哪一块」。
	p.fillRect(crop, QColor(48, 48, 52));
	p.drawImage(QRect(topLeft, dispSize), m_source);

	// 图片没铺满裁剪框时，把图片自身的边界画出来（虚线），避免误以为空隙也是头像内容
	const QRect imageRect(topLeft, dispSize);
	if (!imageRect.contains(crop)) {
		p.setRenderHint(QPainter::Antialiasing, false);
		p.setPen(QPen(QColor(255, 255, 255, 110), 1, Qt::DashLine));
		p.drawRect(imageRect.adjusted(0, 0, -1, -1));
	}

	// 裁剪框外压暗，框内保持原样
	const QColor shade(0, 0, 0, 160);
	const QRect area     = paintArea();
	p.fillRect(QRect(area.left(), area.top(), area.width(), crop.top() - area.top()), shade);
	p.fillRect(QRect(area.left(), crop.bottom() + 1, area.width(), area.bottom() - crop.bottom()), shade);
	p.fillRect(QRect(area.left(), crop.top(), crop.left() - area.left(), crop.height()), shade);
	p.fillRect(QRect(crop.right() + 1, crop.top(), area.right() - crop.right(), crop.height()), shade);

	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(QColor(255, 255, 255, 220), 2));
	p.drawRect(crop.adjusted(0, 0, -1, -1));
	p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
	p.drawEllipse(crop.adjusted(1, 1, -1, -1));
}

void AvatarCropDialog::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton) {
		m_dragging   = true;
		m_dragOrigin = event->position();
		event->accept();
		return;
	}
	QDialog::mousePressEvent(event);
}

void AvatarCropDialog::mouseMoveEvent(QMouseEvent *event) {
	if (m_dragging) {
		const QPointF delta = event->position() - m_dragOrigin;
		m_dragOrigin        = event->position();
		m_offset += delta;
		clampOffset();
		update();
		event->accept();
		return;
	}
	QDialog::mouseMoveEvent(event);
}

void AvatarCropDialog::mouseReleaseEvent(QMouseEvent *event) {
	if ((event->button() == Qt::LeftButton) && m_dragging) {
		m_dragging = false;
		event->accept();
		return;
	}
	QDialog::mouseReleaseEvent(event);
}

void AvatarCropDialog::wheelEvent(QWheelEvent *event) {
	if (m_zoomSlider) {
		// 每格 10%，从「整图」到「4 倍铺满」大约 33 格，滚起来不至于手酸
		const int step = (event->angleDelta().y() > 0) ? 10 : -10;
		m_zoomSlider->setValue(qBound(m_zoomSlider->minimum(), m_zoomSlider->value() + step, m_zoomSlider->maximum()));
		event->accept();
		return;
	}
	QDialog::wheelEvent(event);
}

void AvatarCropDialog::resizeEvent(QResizeEvent *event) {
	QDialog::resizeEvent(event);
	updateBaseZoom();
	clampOffset();
	updateHint();
}

void AvatarCropDialog::onZoomSliderChanged(int value) {
	m_zoomFactor = value / 100.0;
	clampOffset();
	updateHint();
	update();
}

void AvatarCropDialog::resetView() {
	m_zoomFactor = 1.0;
	m_offset     = QPointF(0.0, 0.0);
	if (m_zoomSlider) {
		m_zoomSlider->setValue(100);
	}
	updateBaseZoom();
	clampOffset();
	updateHint();
	update();
}

QImage AvatarCropDialog::croppedImage(int side) const {
	if (side <= 0) {
		side = 512;
	}
	QImage out(side, side, QImage::Format_ARGB32);
	out.fill(Qt::transparent);

	if (m_source.isNull()) {
		return out;
	}

	const QRect crop = cropRect();
	const qreal zoom = effectiveZoom();
	const QSize dispSize(qRound(m_source.width() * zoom), qRound(m_source.height() * zoom));
	const QPoint topLeft(qRound(crop.center().x() + m_offset.x() - dispSize.width() / 2.0),
						 qRound(crop.center().y() + m_offset.y() - dispSize.height() / 2.0));

	const QRectF srcRect((crop.left() - topLeft.x()) / zoom, (crop.top() - topLeft.y()) / zoom,
						 crop.width() / zoom, crop.height() / zoom);
	const QRectF sourceBounds(0, 0, m_source.width(), m_source.height());
	const QRectF clipped = srcRect.intersected(sourceBounds);
	if (clipped.isEmpty()) {
		return out;
	}

	const QRectF target((clipped.left() - srcRect.left()) / srcRect.width() * side,
						(clipped.top() - srcRect.top()) / srcRect.height() * side,
						clipped.width() / srcRect.width() * side, clipped.height() / srcRect.height() * side);

	QPainter p(&out);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.drawImage(target, m_source, clipped);
	p.end();
	return out;
}
