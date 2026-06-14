class CmdRequest {
  final String cmd;
  final Map<String, dynamic> params;
  final String requestId;

  const CmdRequest({
    required this.cmd,
    this.params = const {},
    required this.requestId,
  });

  Map<String, dynamic> toJson() => {
        'cmd': cmd,
        'params': params,
        'request_id': requestId,
      };
}

class CmdResponse {
  final String event;
  final String cmd;
  final bool success;
  final Map<String, dynamic>? data;
  final String requestId;

  const CmdResponse({
    required this.event,
    required this.cmd,
    required this.success,
    this.data,
    required this.requestId,
  });

  factory CmdResponse.fromJson(Map<String, dynamic> json) {
    return CmdResponse(
      event: json['event'] as String? ?? '',
      cmd: json['cmd'] as String? ?? '',
      success: json['success'] as bool? ?? false,
      data: json['data'] as Map<String, dynamic>?,
      requestId: json['request_id'] as String? ?? '',
    );
  }
}
