$(function() {
	var options = {
		lines: { show: true },
		points: { show: true },
		xaxis: { mode: 'time', tickLength: 5 },
		yaxis: { },
	};

	function reload_graph() {
		var url = 'http://127.0.0.1:8080/nodes/999/series/0?npoints=200'; //rx

		// Restrict to last couple of hours
		end = new Date().getTime();
		start = new Date(end - 7200000);
		start = Math.floor(start / 1000);
		end = Math.floor(end / 1000);
		url = url + "&start=" + start + "&end=" + end;

		function onDataReceived(series) {
			$.plot('#graph', [series], options);
		}

		$.ajax({
			url: url,
			method: 'GET',
			dataType: 'json',
			success: onDataReceived
		});
	}

	function get_current() {
		var url = 'http://127.0.0.1:8080/nodes/999/values';

		reload_graph();

		function onDataReceived(values) {
			$('#lastupdate').html('' + new Date(values.timestamp));
			$('#rxrate').html(values.values[0]);
			$('#txrate').html(values.values[1]);
		}
		$.ajax({
			url: url,
			method: 'GET',
			dataType: 'json',
			success: onDataReceived
		});
	}

	$(document).ready(function () {
		get_current();
		window.setInterval(get_current, 10000);
	});
});
