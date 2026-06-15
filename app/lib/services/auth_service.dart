import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

class AuthState {
  final bool isLoggedIn;
  final String? token;
  final String? email;
  const AuthState({this.isLoggedIn = false, this.token, this.email});
  AuthState copyWith({bool? isLoggedIn, String? token, String? email}) =>
      AuthState(isLoggedIn: isLoggedIn ?? this.isLoggedIn, token: token ?? this.token, email: email ?? this.email);
}

class AuthService {
  static const _keyToken = 'jwt_token';
  static const _keyEmail = 'user_email';

  Future<AuthState> loadAuth() async {
    final prefs = await SharedPreferences.getInstance();
    final token = prefs.getString(_keyToken);
    final email = prefs.getString(_keyEmail);
    return AuthState(isLoggedIn: token != null, token: token, email: email);
  }

  Future<void> saveAuth(String token, String email) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_keyToken, token);
    await prefs.setString(_keyEmail, email);
  }

  Future<void> logout() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_keyToken);
    await prefs.remove(_keyEmail);
  }
}

final authServiceProvider = Provider<AuthService>((ref) => AuthService());

final authProvider = NotifierProvider<AuthNotifier, AuthState>(
  AuthNotifier.new,
);

class AuthNotifier extends Notifier<AuthState> {
  @override
  AuthState build() {
    Future.microtask(() => _init());
    return const AuthState();
  }

  Future<void> _init() async {
    final authService = ref.read(authServiceProvider);
    state = await authService.loadAuth();
  }

  Future<void> login(String token, String email) async {
    await ref.read(authServiceProvider).saveAuth(token, email);
    state = AuthState(isLoggedIn: true, token: token, email: email);
  }

  Future<void> logout() async {
    await ref.read(authServiceProvider).logout();
    state = const AuthState();
  }
}
