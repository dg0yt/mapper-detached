/*
 *    Copyright 2019 Kai Pastor
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

#include "powershell_position_source.h"

#include <QFile>
#include <QFileDevice>


inline void initMapperWindowsResources()
{
	Q_INIT_RESOURCE(powershell_position_source);
}


namespace OpenOrienteering
{

PowershellPositionSource::PowershellPositionSource(QObject* parent)
: PowershellPositionSource(defaultScript(), parent)
{
	// nothing else
}

PowershellPositionSource::PowershellPositionSource(QByteArray&& script, QObject* parent)
: QGeoPositionInfoSource(parent)
, powershell_script(std::move(script))
{
	if (powershell_script.isEmpty())
	{
		setError(QGeoPositionInfoSource::UnknownSourceError);
		return;
	}
	
	powershell.setProgram(QLatin1String("powershell.exe"));
	powershell.setArguments(QString(QStringLiteral("-NoLogo -NoProfile -NonInteractive -Command -")).split(QLatin1Char(' ')));
	powershell.setReadChannel(QProcess::StandardOutput);
	connect(&powershell, &QProcess::stateChanged, this, &PowershellPositionSource::powershellStateChanged);
	connect(&powershell, &QProcess::readyReadStandardOutput, this, &PowershellPositionSource::readStandardOutput);
	connect(&powershell, &QProcess::readyReadStandardError, this, &PowershellPositionSource::readStandardError);
	
	periodic_update_timer.setSingleShot(true);
	connect(&periodic_update_timer, &QTimer::timeout, this, &PowershellPositionSource::periodicUpdateTimeout);
	
	single_update_timer.setSingleShot(true);
	connect(&single_update_timer, &QTimer::timeout, this, &PowershellPositionSource::singleUpdateTimeout);
}

PowershellPositionSource::~PowershellPositionSource()
{
	if (powershell.state() != QProcess::NotRunning
	    && !powershell.waitForFinished(1000))
	{
		powershell.kill();
	}
}

// static
QByteArray PowershellPositionSource::defaultScript()
{
	QByteArray script;
	QFile script_file(QStringLiteral(":/sensors/powershell_position_source.ps1"));
	if (script_file.open(QIODevice::ReadOnly))
		script = script_file.readAll();
	if (script_file.error() != QFileDevice::NoError)
		script.clear();
	return script;
}

const QByteArray& PowershellPositionSource::script() const
{
	return powershell_script;
}

QGeoPositionInfoSource::Error PowershellPositionSource::error() const
{
	return position_error;
}

QGeoPositionInfo PowershellPositionSource::lastKnownPosition(bool /* satellite_only */) const
{
	return last_position;
}

QGeoPositionInfoSource::PositioningMethods PowershellPositionSource::supportedPositioningMethods() const
{
	switch (position_error)
	{
	case NoError:
		return AllPositioningMethods;
	default:
		return NoPositioningMethods;
	}
}

int PowershellPositionSource::minimumUpdateInterval() const
{
	return 1000;
}

void PowershellPositionSource::startUpdates()
{
	if (!init())
		return;
	
	updates_ongoing = true;
	periodic_update_timer.start();
}

void PowershellPositionSource::stopUpdates()
{
	if (updates_ongoing)
	{
		periodic_update_timer.stop();
		updates_ongoing = false;
		powershell.kill();
	}
}

void PowershellPositionSource::requestUpdate(int timeout)
{
	if (!init())
		return;
	
	setError(QGeoPositionInfoSource::NoError);
	if (timeout == 0)
	{
		timeout = 120000; // 2 min for cold start
	}
	else if (timeout < minimumUpdateInterval())
	{
		emit updateTimeout();
		return;
	}
	
	single_update_timer.start(timeout);
}

bool PowershellPositionSource::init()
{
	if (powershell.state() != QProcess::NotRunning)
		return true;
	
	powershell.start();
	if (powershell.state() == QProcess::NotRunning)
	{
		updates_ongoing = false;
		setError(QGeoPositionInfoSource::UnknownSourceError);
		return false;
	}
	
	periodic_update_timer.setInterval(updateInterval());
	return true;
}

void PowershellPositionSource::powershellStateChanged(QProcess::ProcessState new_state)
{
	switch (new_state)
	{
	case QProcess::Starting:
		// nothing
		break;
	case QProcess::Running:
		powershell.write(powershell_script);
		if (single_update_timer.isActive())
			powershell.write("& $position() \r\n");
		break;
	case QProcess::NotRunning:
		updates_ongoing = false;
		break;
	}
}

void PowershellPositionSource::readStandardError()
{
	auto const error_output = powershell.readAllStandardError();
	qWarning("!!! %s", error_output.data());
}

void PowershellPositionSource::readStandardOutput()
{
	while (!powershell.atEnd())
	{
		auto const line = powershell.readLine(100).trimmed();
		if (line.startsWith("Position;"))
			processPosition(line);
		else if (line.startsWith("Status;"))
			processStatus(line);
		else if (line.startsWith("Permission;"))
			processPermission(line);
		else if (!line.isEmpty())
			qDebug("Unknown sequence: '%s'", line.data());
	}
	
	if (updates_ongoing)
		powershell.write("Start-Sleep -Milliseconds 1000; & $location \r\n");
	else
		powershell.kill();
}

void PowershellPositionSource::processPosition(const QByteArray& line)
{
	Q_ASSERT(line.startsWith("Position;"));
	
	auto pos = 0;
	auto next_pos = line.indexOf(';', pos + 1);
	auto const read_cstring = [&]() -> QByteArray {
	                          pos = next_pos + 1;
	                          next_pos = line.indexOf(';', pos);
	                          return QByteArray::fromRawData(line.constData() + pos, next_pos - pos);
};
	auto const read_string = [&]() -> QString {
	                         pos = next_pos + 1;
	                         next_pos = line.indexOf(';', pos);
	                         return QString::fromLatin1(line.constData() + pos, next_pos - pos);
};
	
	auto const status = read_cstring();
	if (status != "Ready")
	{
		setError(ClosedError);
		return;
	}
	
	auto const date_time  = QDateTime::fromString(read_string(), Qt::ISODate);
	bool numbers_ok[5];
	auto latitude   = read_string().toDouble(&numbers_ok[0]);
	auto longitude  = read_string().toDouble(&numbers_ok[1]);
	auto altitude   = read_string().toDouble(&numbers_ok[2]);
	auto const h_accuracy = read_string().toDouble(&numbers_ok[3]);
	auto const v_accuracy = read_string().toDouble(&numbers_ok[4]);
	
	using std::begin; using std::end;
	if (pos == 0
	    || !date_time.isValid()
	    || std::find(begin(numbers_ok), end(numbers_ok), false) != end(numbers_ok))
	{
		qDebug("Could not parse location '%s'", line.data());
		setError(UnknownSourceError);
		return;
	}
	
	if (qIsNaN(h_accuracy))
	{
		qDebug("Horizontal accuracy unknown");
		return;
	}
	
	auto geo_coord = QGeoCoordinate{latitude, longitude};
	if (!qIsNaN(v_accuracy))
		geo_coord.setAltitude(altitude);
	
	auto position = QGeoPositionInfo{geo_coord, date_time};
	position.setAttribute(QGeoPositionInfo::HorizontalAccuracy, h_accuracy);
	if (!qIsNaN(v_accuracy))
		position.setAttribute(QGeoPositionInfo::VerticalAccuracy, v_accuracy);
	
	nativePositionUpdate(last_position);
}

void PowershellPositionSource::processStatus(const QByteArray& line)
{
	qDebug("TODO: '%s'", line.data());
}

void PowershellPositionSource::processPermission(const QByteArray& line)
{
	qDebug("TODO: '%s'", line.data());
}

void PowershellPositionSource::nativePositionUpdate(const QGeoPositionInfo& position)
{
	periodic_update_timer.stop();
	last_position = position;
	if (updates_ongoing)
	{
		periodic_update_timer.start();
	}
	if (single_update_timer.isActive())
	{
		single_update_timer.stop();
	}
	setError(NoError);
	emit positionUpdated(last_position);
}

void PowershellPositionSource::periodicUpdateTimeout()
{
	if (last_position.isValid())
	{
		auto virtual_position = last_position;
		virtual_position.setTimestamp(last_position.timestamp().addMSecs(updateInterval()));
		last_position = virtual_position;
		emit positionUpdated(last_position);
	}
	periodic_update_timer.start();
}

void PowershellPositionSource::singleUpdateTimeout()
{
	if (single_update_timer.isActive())
		emit updateTimeout();
}

void PowershellPositionSource::setError(QGeoPositionInfoSource::Error position_error)
{
	this->position_error = position_error;
    if (position_error != NoError)
        emit this->QGeoPositionInfoSource::error(position_error);
}


}  // namespace OpenOrienteering
