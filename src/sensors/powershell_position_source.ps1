#
#    Copyright 2019 Kai Pastor
#
#    This file is part of OpenOrienteering.
#
#    OpenOrienteering is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    OpenOrienteering is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.

Add-Type -AssemblyName System.Device
$watcher = New-Object System.Device.Location.GeoCoordinateWatcher -arg 'High'
#$watcher.MovementThreshold = 0

$statusChangedAction = {
	$status_ = $EventArgs.Status
	$line_ = "Status;$status_"
    $line_ | Write-Host
}
$statusJob = Register-ObjectEvent -InputObject $watcher -EventName StatusChanged -Action $statusChangedAction

$positionChangedAction = {
	$time_ = $EventArgs.Position.Timestamp.UtcDateTime
	$loc_ = $EventArgs.Position.Location
	$lat_ = $loc_.Latitude
	$lon_ = $loc_.Longitude
	$alt_ = $loc_.Altitude
	$hac_ = $loc_.HorizontalAccuracy
	$vac_ = $loc_.VerticalAccuracy
	# "" string interpretation does not use the culture, -f does.
	$line_ = "Position;{0:u};$lat_;$lon_;$alt_;$hac_;$vac_" -f ($time_, )
    $line_ | Write-Host
}
$positionJob = Register-ObjectEvent -InputObject $watcher -EventName PositionChanged -Action $positionChangedAction

$position = {
	$time_ = $watcher.Position.Timestamp.UtcDateTime
	$loc_ = $watcher.Position.Location
	# String interpretation does not use the culture, -f does.
	$lat_ = $loc_.Latitude
	$lon_ = $loc_.Longitude
	$alt_ = $loc_.Altitude
	$hac_ = $loc_.HorizontalAccuracy
	$vac_ = $loc_.VerticalAccuracy
	$line_ = "Position;{0:u};$lat_;$lon_;$alt_;$hac_;$vac_" -f ($time_, )
    $line_ | Write-Host
}

$status = {
	$status_ = $watcher.Status
	$line_ = "Status;$status_;***"
    $line_ | Write-Host
}

$accuracy = {
	$accuracy_ = $watcher.DesiredAccuracy
	$line_ = "Accuracy;$accuracy_;***"
    $line_ | Write-Host
}

$permission = {
	$permission_ = $watcher.Permission
	$line_ = "Permission;$permission_;***"
    $line_ | Write-Host
}

$watcher.start()
