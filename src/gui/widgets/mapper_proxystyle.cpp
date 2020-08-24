/*
 *    Copyright 2013-2020 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "mapper_proxystyle.h"

#include <Qt>
#include <QBrush>
#include <QCommonStyle> // IWYU pragma: keep
#include <qdrawutil.h>
#include <QApplication>
#include <QFlags>
#include <QFormLayout>  // IWYU pragma: keep
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QSize>
#include <QStyleOption>
#include <QVariant>
#include <QWidget>

#include "settings.h"
#include "gui/scaling_icon_engine.h"
#include "gui/widgets/segmented_button_layout.h"
#include "gui/util_gui.h"


namespace OpenOrienteering {

namespace  {

/**
 * This function recognizes dock widget related widgets by the class name
 * matching "DockWidget".
 * 
 * This function helps to customize the style for classes like
 * "QDockWidgetTitleButton" or "MapEditorDockWidget".
 */
bool Q_DECL_UNUSED isDockWidgetRelated(const QWidget* widget)
{
	if (widget == nullptr)
		return false;
	
	auto* raw_class_name = widget->metaObject()->className();
	auto const class_name = QByteArray::fromRawData(raw_class_name, static_cast<int>(qstrlen(raw_class_name)));
	return class_name.contains("DockWidget");
}

}  // namespace



MapperProxyStyle::MapperProxyStyle(QStyle* base_style)
 : QProxyStyle(base_style)
{
	auto& settings = Settings::getInstance();
	onSettingsChanged(settings);
	connect(&settings, &Settings::settingsChanged, this, [this]() {
		onSettingsChanged(*qobject_cast<Settings*>(sender()));
	});
}

MapperProxyStyle::~MapperProxyStyle() = default;


void MapperProxyStyle::onSettingsChanged(const Settings& settings)
{
	auto const old_mode = touch_mode;
	auto const old_size = button_size;
	
	if (!settings.touchModeEnabled() && touch_mode)
	{
		touch_mode = false;
		toolbar = {};
		menu = {};
		menu_font = QApplication::font();
	}
	else if (settings.touchModeEnabled() &&
	         (!touch_mode || settings.getSetting(Settings::ActionGridBar_ButtonSizeMM) != button_size))
	{
		touch_mode = true;
		
		button_size = settings.getSetting(Settings::ActionGridBar_ButtonSizeMM);
		{
			auto const button_size_mm = button_size.toReal();
			auto const button_size_pixel = qRound(Util::mmToPixelPhysical(button_size_mm));
			auto const margin_size_pixel = button_size_pixel / 4;
			toolbar.icon_size = button_size_pixel - margin_size_pixel;
			
			auto const scale_factor = qreal(toolbar.icon_size) / QProxyStyle::pixelMetric(PM_ToolBarIconSize);
			toolbar.item_spacing = std::max(1, margin_size_pixel - 2 * qRound(scale_factor));
			toolbar.separator_extent = qRound(QProxyStyle::pixelMetric(PM_ToolBarSeparatorExtent) * scale_factor);
			toolbar.extension_extent = qRound(QProxyStyle::pixelMetric(PM_ToolBarExtensionExtent) * scale_factor);
		}
		
		small_icon_size = qMax(QProxyStyle::pixelMetric(QStyle::PM_ButtonIconSize), int(0.7 * toolbar.icon_size));
		{
			auto const scale_factor = qreal(small_icon_size) / QProxyStyle::pixelMetric(QStyle::PM_ButtonIconSize);
			menu.button_indicator = qRound(QProxyStyle::pixelMetric(PM_MenuButtonIndicator) * scale_factor);
			menu.h_margin = qRound(QProxyStyle::pixelMetric(PM_MenuHMargin) * scale_factor);
			menu.v_margin = qRound(QProxyStyle::pixelMetric(PM_MenuVMargin) * scale_factor);
			menu.panel_width = qRound(QProxyStyle::pixelMetric(PM_MenuPanelWidth) * scale_factor);
			menu.scroller_height = qRound(QProxyStyle::pixelMetric(PM_MenuScrollerHeight) * scale_factor);
		}
		
		menu_font = QApplication::font();
		{
			auto const menu_font_size = small_icon_size - 4;  // cf. QMenu's action item rect calculation.
			if (menu_font_size > original_font.pixelSize())
			{
				menu_font.setPixelSize(2 * menu_font_size);
			}
		}
	}
	else
	{
		// No style-related change
		return;
	}
	
	if (sender() && (old_mode != touch_mode || old_size != button_size))
	{
		// Significant change of settings
		QApplication::setStyle(new MapperProxyStyle());
	}
}


void MapperProxyStyle::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
	auto overridden_element = element;
	switch (element)
	{
	case PE_IndicatorButtonDropDown:
		overridden_element = PE_PanelButtonTool; // Enforce same appearance. Workaround QWindowsStyle quirk.
		Q_FALLTHROUGH();
	case PE_PanelButtonCommand:
	case PE_PanelButtonBevel:
	case PE_PanelButtonTool:
		if (int segment = widget ? widget->property("segment").toInt() : 0)
		{
			drawSegmentedButton(segment, overridden_element, option, painter, widget);
			return;
		}
		if (touch_mode
		    && element == PE_PanelButtonTool
		    && option
		    && option->state & State_On)
		{
			auto const& window_color = option->palette.window().color();
			auto const fill = QBrush(qGray(window_color.rgb()) > 127 ? window_color.darker(125) : window_color.lighter(125));
			painter->setPen(Qt::NoPen);
			painter->setBrush(fill);
			painter->drawRoundedRect(option->rect, 5.0, 5.0, Qt::AbsoluteSize);
			return;
		}
		break;
		
#ifdef Q_OS_ANDROID
	case QStyle::PE_IndicatorItemViewItemCheck:
		if (option->state.testFlag(QStyle::State_NoChange)
		    || !option->state.testFlag(QStyle::State_Enabled))
		{
			auto item = qstyleoption_cast<const QStyleOptionViewItem*>(option);
			auto o = QStyleOptionViewItem{ *item };
			o.state |= QStyle::State_Enabled;
			if (option->state.testFlag(QStyle::State_NoChange))
			{
				o.state &= ~QStyle::State_NoChange;
				o.state |= QStyle::State_On;
			}
			auto opacity = painter->opacity();
			painter->setOpacity(0.4);
			QProxyStyle::drawPrimitive(element, &o, painter, widget);
			painter->setOpacity(opacity);
			return;
		}
		break;
#endif
		
	default:
		; // Nothing
	}
	
	QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void MapperProxyStyle::drawSegmentedButton(int segment, QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
	painter->save();
	
	// Background (to be clipped by the widget)
	int left_adj  = (segment & SegmentedButtonLayout::LeftNeighbor)  ? 4 : 0;
	int right_adj = (segment & SegmentedButtonLayout::RightNeighbor) ? 4 : 0;
	
	if (option->rect.left())
	{
		// Subcomponent clipping
		painter->setClipRect(option->rect, Qt::IntersectClip);
		left_adj = 4;
	}
	
	QStyleOption mod_option(*option);
	mod_option.rect.adjust(-left_adj, 0, right_adj, 0);
	QProxyStyle::drawPrimitive(element, &mod_option, painter, widget);
	
	// Segment separators
	painter->setOpacity((option->state & QStyle::State_Enabled) ? 0.5 : 0.2);
	int frame_width = proxy()->pixelMetric(PM_DefaultFrameWidth, option, widget);
	mod_option.rect = option->rect.adjusted(0, frame_width, 0, -frame_width);
	
	if (left_adj)
	{
		if (option->state & QStyle::State_Sunken)
		    painter->setPen(option->palette.dark().color());
		else
		    painter->setPen(option->palette.light().color());
		painter->drawLine(option->rect.left(), option->rect.top(), option->rect.left(), option->rect.bottom());
	}
	
	if (right_adj)
	{
		if (option->state & QStyle::State_Sunken)
		    painter->setPen(option->palette.light().color());
		else
		    painter->setPen(option->palette.dark().color());
		painter->drawLine(option->rect.right(), option->rect.top(), option->rect.right(), option->rect.bottom());
	}
	
	painter->restore();
}

int MapperProxyStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
	switch (metric)
	{
	case QStyle::PM_ToolBarIconSize:
		if (touch_mode)
			return toolbar.icon_size;
#ifdef Q_OS_MACOS
		{
			static int s = (QProxyStyle::pixelMetric(metric) + QProxyStyle::pixelMetric(QStyle::PM_SmallIconSize)) / 2;
			return s;
		}
#endif
		break;
	case QStyle::PM_ToolBarItemSpacing:
		if (touch_mode)
			return toolbar.item_spacing;
		break;
	case QStyle::PM_ToolBarSeparatorExtent:
		if (touch_mode)
			return toolbar.separator_extent;
		break;
	case QStyle::PM_ToolBarExtensionExtent:
		if (touch_mode)
			return toolbar.extension_extent;
		break;
	case PM_MenuHMargin:
		if (touch_mode)
			return menu.h_margin;
		break;
	case PM_MenuVMargin:
		if (touch_mode)
			return menu.v_margin;
		break;
	case PM_MenuPanelWidth:
		if (touch_mode)
			return menu.panel_width;
		break;
	case PM_MenuScrollerHeight:
		if (touch_mode)
			return menu.scroller_height;
		break;
	case QStyle::PM_ButtonIconSize:
	case QStyle::PM_SmallIconSize:
		if (touch_mode)
			return small_icon_size;
		break;
	case QStyle::PM_DockWidgetSeparatorExtent:
	case QStyle::PM_SplitterWidth:
		if (touch_mode)
			return (QProxyStyle::pixelMetric(metric) + small_icon_size) / 2;
		break;
	default:
		break;
	}
	
	return QProxyStyle::pixelMetric(metric, option, widget);
}

QSize MapperProxyStyle::sizeFromContents(QStyle::ContentsType ct, const QStyleOption* opt, const QSize& contents_size, const QWidget* w) const
{
	switch (ct)
	{
#ifdef Q_OS_ANDROID
	case QStyle::CT_SizeGrip:
		{
			auto width = qMax(QProxyStyle::pixelMetric(QStyle::PM_ButtonIconSize), QProxyStyle::pixelMetric(QStyle::PM_IndicatorWidth));
			return { width, width };
		}
		break;
#endif
	default:
		break;
	}
	
	return QProxyStyle::sizeFromContents(ct, opt, contents_size, w);
}

QIcon MapperProxyStyle::standardIcon(QStyle::StandardPixmap standard_icon, const QStyleOption* option, const QWidget* widget) const
{
	QIcon icon;
	switch (standard_icon)
	{
#ifdef Q_OS_ANDROID
	// Cf. https://code.qt.io/cgit/qt/qtbase.git/tree/src/widgets/styles/qfusionstyle.cpp?h=5.12#n3785
	case QStyle::SP_TitleBarNormalButton:
	case QStyle::SP_TitleBarCloseButton:
		if (auto* common_style = qobject_cast<QCommonStyle*>(baseStyle()))
		{
			icon = common_style->QCommonStyle::standardIcon(standard_icon, option, widget);
		}
		break;
#endif
	default:
		break;
	}
	
	if (icon.isNull())
		icon = QProxyStyle::standardIcon(standard_icon, option, widget);
	if (icon.actualSize(QSize(1000,1000)).width() < 1000)
		icon = QIcon(new ScalingIconEngine(icon));
	return icon;
}

QPixmap MapperProxyStyle::standardPixmap(QStyle::StandardPixmap standard_pixmap, const QStyleOption* option, const QWidget* widget) const
{
	switch (standard_pixmap)
	{
#ifdef Q_OS_ANDROID
	// Cf. https://code.qt.io/cgit/qt/qtbase.git/tree/src/widgets/styles/qfusionstyle.cpp?h=5.12#n3807
	case QStyle::SP_TitleBarNormalButton:
	case QStyle::SP_TitleBarCloseButton:
		if (auto* common_style = qobject_cast<QCommonStyle*>(baseStyle()))
		{
			return common_style->QCommonStyle::standardPixmap(standard_pixmap, option, widget);
		}
		break;
#endif
	default:
		break;
	}
	
	return QProxyStyle::standardPixmap(standard_pixmap, option, widget);
}

int MapperProxyStyle::styleHint(QStyle::StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* return_data) const
{
	switch (hint)
	{
#ifdef Q_OS_ANDROID
	case QStyle::SH_FormLayoutWrapPolicy:
		return QFormLayout::WrapLongRows;
#endif		
	default:
		break;
	}
	
	return QProxyStyle::styleHint(hint, option, widget, return_data);
}


void MapperProxyStyle::polish(QApplication* application)
{
	QProxyStyle::polish(application);
	QApplication::setFont(menu_font, "QComboMenuItem");
}

void MapperProxyStyle::unpolish(QApplication* application)
{
	QApplication::setFont(QApplication::font(), "QComboMenuItem");
	QProxyStyle::unpolish(application);
}


void MapperProxyStyle::polish(QWidget* widget)
{
	QProxyStyle::polish(widget);
	
	if (touch_mode)
	{
		if (auto* menu = qobject_cast<QMenu*>(widget))
		{
			qDebug("setFont");
			menu->setFont(menu_font);
		}
	}	
}

void MapperProxyStyle::unpolish(QWidget* widget)
{
	if (auto* menu = qobject_cast<QMenu*>(widget))
	{
		if (menu->font().isCopyOf(menu_font))
			menu->setFont(original_font);
	}
	
	QProxyStyle::unpolish(widget);
}


}  // namespace OpenOrienteering
