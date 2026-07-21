#!/usr/bin/env python3
'''
Shared linear-feature TDLeaf(lambda) support for board-game CLIs.

The evaluator and all CLI features are assumed to be from the point of view of *the side to move*. Consequently:

    target_t = -V(s_{t+1})
    e_t      = grad_t - lambda * e_{t-1}

The alternating sign in the trace recurrence is required because each ply changes the player whose point of view defines the value.
'''

from __future__ import annotations

import atexit
import json
import math
import random
import subprocess
import tempfile
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

import numpy as np

@dataclass(frozen=True)
class GameProfile:
	name: str
	board_squares: int
	promotion_codes: Dict[str, int]
	transcript_result_pov: str = 'white'
	first_player: str = 'white'
	default_max_plies: int = 400

GAME_PROFILES: Dict[str, GameProfile] = {'chess': GameProfile(name='Chess', board_squares=64, promotion_codes={'N': 1, 'B': 2, 'R': 3, 'Q': 4}, default_max_plies=400),                                 \
                                         'capablanca': GameProfile(name='Capablanca Chess', board_squares=80, promotion_codes={'N': 1, 'B': 2, 'R': 3, 'A': 4, 'C': 5, 'Q': 6}, default_max_plies=500), \
                                         'grandchess': GameProfile(name='Grand Chess', board_squares=100, promotion_codes={'N': 1, 'B': 2, 'R': 3, 'C': 4, 'M': 5, 'Q': 6}, default_max_plies=500),     \
                                         'loa': GameProfile(name='Lines of Action', board_squares=64, promotion_codes={}, transcript_result_pov='first', first_player='first', default_max_plies=300)   }

_GAME_TRANSCRIPT_FORMAT = 'tdleaf-game-transcript-v1'

#  Return the default append-only JSONL transcript path for a checkpoint.
def default_game_transcript_path(checkpoint_path: str) -> str:
	path = Path(checkpoint_path)
	return str(path.with_name(f'{path.stem}-games.jsonl'))

#  Append one complete, reconstructible game record per JSONL line.
class GameTranscriptWriter:

	def __init__(self, path: str) -> None:
		self.path = str(path)
		self._handle = None
		if self.path:
			output = Path(self.path)
			output.parent.mkdir(parents=True, exist_ok=True)
																	#  Append mode preserves transcripts when training resumes.
																	#  Line buffering plus an explicit flush after each record limits loss to an unfinished game.
			self._handle = output.open('a', encoding='utf-8', buffering=1)
		atexit.register(self.close)

	@property
	def enabled(self) -> bool:
		return self._handle is not None

	def write_game(self, record: Dict[str, Any]) -> None:
		if self._handle is None:
			return
		payload = dict(record)
		payload.setdefault('format', _GAME_TRANSCRIPT_FORMAT)
		line = json.dumps(payload, separators=(',', ':'), ensure_ascii=False)
		self._handle.write(line + '\n')
		self._handle.flush()

	def close(self) -> None:
		handle = self._handle
		self._handle = None
		if handle is not None:
			handle.close()

	def __enter__(self) -> 'GameTranscriptWriter':
		return self

	def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
		self.close()

class _LRU:
	def __init__(self, capacity: int):
		self.capacity = max(0, int(capacity))
		self.data: OrderedDict[Any, Any] = OrderedDict()

	def get(self, key: Any) -> Tuple[bool, Any]:
		if key not in self.data:
			return False, None
		value = self.data.pop(key)
		self.data[key] = value
		return True, value

	def put(self, key: Any, value: Any) -> None:
		if self.capacity <= 0:
			return
		if key in self.data:
			self.data.pop(key)
		self.data[key] = value
		while len(self.data) > self.capacity:
			self.data.popitem(last=False)

	def clear(self) -> None:
		self.data.clear()

	def __len__(self) -> int:
		return len(self.data)

'''
JSON CLI client with persistent-process support and immutable-state caches.

A persistent CLI must read one JSON request per input line, write one JSON response per line, flush stdout, and continue until EOF.
If the executable still supports only one request per process, this client detects the exit and falls back to subprocess.run automatically.
'''
class CachedCLI:

	def __init__(self, exe_path: str, *, prefer_persistent: bool=True, cache_entries: int=250_000) -> None:
		self.exe_path = exe_path
		self.prefer_persistent = bool(prefer_persistent)
		self._persistent_available: Optional[bool] = None
		self._proc: Optional[subprocess.Popen[str]] = None
		self._stderr_file = tempfile.TemporaryFile(mode='w+t', encoding='utf-8')
		self._cache = _LRU(cache_entries)
		self.calls = 0
		self.cache_hits = 0
		self.process_starts = 0
		atexit.register(self.close)

	def _start_process(self) -> None:
		self.close_process_only()
		self._stderr_file.seek(0)
		self._stderr_file.truncate(0)
		self._proc = subprocess.Popen([self.exe_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._stderr_file, text=True, bufsize=1)
		self.process_starts += 1

	def _stderr_text(self) -> str:
		self._stderr_file.flush()
		self._stderr_file.seek(0)
		text = self._stderr_file.read()
		self._stderr_file.seek(0, 2)
		return text

	def _call_persistent(self, payload: Dict[str, Any]) -> Dict[str, Any]:
		if self._proc is not None and self._proc.poll() is not None:
			if self._persistent_available is True:
				self._persistent_available = False
				self.close_process_only()
				raise RuntimeError('CLI exited after a prior response; switching to one-shot mode')
			self.close_process_only()
		if self._proc is None:
			self._start_process()
		assert self._proc is not None
		assert self._proc.stdin is not None
		assert self._proc.stdout is not None

		try:
			self._proc.stdin.write(json.dumps(payload, separators=(',', ':')) + '\n')
			self._proc.stdin.flush()
		except (BrokenPipeError, OSError) as exc:
			raise RuntimeError(f'Persistent CLI pipe failed: {exc}') from exc

		line = self._proc.stdout.readline()
		if not line:
			rc = self._proc.poll()
			stderr = self._stderr_text()
			raise RuntimeError(f'Persistent CLI ended without a response (code={rc}).\nSTDERR:\n{stderr}\nPayload:\n{json.dumps(payload)}')
		try:
			response = json.loads(line)
		except json.JSONDecodeError as exc:
			raise RuntimeError(f'Bad JSON from persistent CLI:\n{line}') from exc
																	#  Probe only once. A legacy one-shot CLI emits one response and exits;
																	#  a JSON-lines server remains alive waiting for the next request.
		if self._persistent_available is None:
			try:
				self._proc.wait(timeout=0.02)
			except subprocess.TimeoutExpired:
				self._persistent_available = True
			else:
				self._persistent_available = False
				self._proc = None
		return response

	def _call_oneshot(self, payload: Dict[str, Any]) -> Dict[str, Any]:
		proc = subprocess.run([self.exe_path], input=json.dumps(payload) + '\n', stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
		self.process_starts += 1
		if proc.returncode != 0:
			raise RuntimeError(f'CLI failed (code={proc.returncode}).\nSTDERR:\n{proc.stderr}\nPayload:\n{json.dumps(payload)}')
		text = proc.stdout.strip()
		if not text:
			raise RuntimeError(f'Empty response from CLI. Payload={payload}')
		try:
			return json.loads(text.splitlines()[-1])
		except json.JSONDecodeError as exc:
			raise RuntimeError(f'Bad JSON from CLI:\n{text}') from exc

	def _call_uncached(self, payload: Dict[str, Any]) -> Dict[str, Any]:
		self.calls += 1
		if self.prefer_persistent and self._persistent_available is not False:
			try:
				return self._call_persistent(payload)
			except RuntimeError:
																	#  A one-shot executable will normally fail on the second request.
																	#  Switch permanently to subprocess.run for compatibility.
				self._persistent_available = False
				self.close_process_only()
		return self._call_oneshot(payload)

	def _call_cached(self, payload: Dict[str, Any], cache_key: Any) -> Dict[str, Any]:
		found, value = self._cache.get(cache_key)
		if found:
			self.cache_hits += 1
			return value
		value = self._call_uncached(payload)
		self._cache.put(cache_key, value)
		return value
																	#  Never cache this call: Capablanca Chess and Lines of Action deliberately
																	#  choose among multiple starting arrangements on every episode reset.
	def startpos(self) -> str:
		return str(self._call_uncached({'cmd': 'startpos'})['state_hex'])

	def terminal(self, state_hex: str) -> Tuple[bool, Optional[str]]:
		r = self._call_cached({'cmd': 'terminal', 'state_hex': state_hex}, ('terminal', state_hex))
		if not bool(r.get('terminal', False)):
			return False, None
		result = str(r['result'])
		if result not in {'win', 'loss', 'draw'}:
			raise RuntimeError(f'Unexpected terminal result: {result!r}')
		return True, result

	def legal_moves(self, state_hex: str) -> List[str]:
		r = self._call_cached({'cmd': 'legal_moves', 'state_hex': state_hex}, ('legal_moves', state_hex))
		return list(r['moves_hex'])

	def apply_move(self, state_hex: str, move_hex: str) -> str:
		r = self._call_cached({'cmd': 'apply_move', 'state_hex': state_hex, 'move_hex': move_hex}, ('apply_move', state_hex, move_hex))
		return str(r['state_hex'])

	def features(self, state_hex: str) -> np.ndarray:
		r = self._call_cached({'cmd': 'features', 'state_hex': state_hex}, ('features', state_hex))
		return np.asarray(r['features'], dtype=np.float32)

	def phase(self, state_hex: str) -> float:
		r = self._call_cached({'cmd': 'phase', 'state_hex': state_hex}, ('phase', state_hex))
		return float(r['phase'])

	def print_move(self, move_hex: str) -> Dict[str, Any]:
		return dict(self._call_cached({'cmd': 'print_move', 'move_hex': move_hex}, ('print_move', move_hex)))

	def draw(self, state_hex: str) -> Dict[str, Any]:
		return dict(self._call_uncached({'cmd': 'draw', 'state_hex': state_hex}))

	def clear_cache(self) -> None:
		self._cache.clear()

	def stats(self) -> Dict[str, int]:
		return {'cli_calls': self.calls,               \
		        'cache_hits': self.cache_hits,         \
		        'process_starts': self.process_starts, \
		        'cache_entries': len(self._cache)      }

	def close_process_only(self) -> None:
		proc = self._proc
		self._proc = None
		if proc is None:
			return
		try:
			if proc.stdin is not None:
				proc.stdin.close()
		except OSError:
			pass
		try:
			proc.wait(timeout=0.5)
		except subprocess.TimeoutExpired:
			proc.terminate()
			try:
				proc.wait(timeout=0.5)
			except subprocess.TimeoutExpired:
				proc.kill()
				proc.wait()

	def close(self) -> None:
		self.close_process_only()
		try:
			self._stderr_file.close()
		except Exception:
			pass

	def __enter__(self) -> 'CachedCLI':
		return self

	def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
		self.close()

@dataclass
class EvalConfig:
	tau: float = 3.0
	feature_scales: Optional[np.ndarray] = None
	fixed_phase: Optional[float] = None

#  Blend phase vectors ordered opening -> endgame.
def phase_alphas(t: float, count: int) -> np.ndarray:
	if count <= 0:
		raise ValueError('At least one weight vector is required')
	if count == 1:
		return np.ones((1, ), dtype=np.float32)
	t = float(max(0.0, min(1.0, t)))
	if count == 3:
		a_opening = max(0.0, 2.0 * t - 1.0)
		a_endgame = max(0.0, 1.0 - 2.0 * t)
		a_middle = 1.0 - a_opening - a_endgame
		return np.asarray([a_opening, a_middle, a_endgame], dtype=np.float32)
																	#  General piecewise-linear interpolation among evenly spaced phase anchors.
	position = (1.0 - t) * (count - 1)								#  opening t=1 -> 0; endgame t=0 -> K-1
	left = int(math.floor(position))
	right = min(count - 1, left + 1)
	frac = position - left
	out = np.zeros((count, ), dtype=np.float32)
	out[left] += float(1.0 - frac)
	out[right] += float(frac)
	return out

#  Tanh value function over one or more phase-specific weight vectors.
class LinearEvaluator:

	def __init__(self, cli: CachedCLI, weights: np.ndarray, cfg: EvalConfig):
		W = np.asarray(weights, dtype=np.float32)
		if W.ndim == 1:
			W = W[None, :]
		if W.ndim != 2:
			raise ValueError(f'weights must be rank 1 or 2, got shape {W.shape}')
		self.cli = cli
		self.W = W.copy()
		self.cfg = cfg
		if cfg.feature_scales is None:
			self.scales = np.ones((W.shape[1], ), dtype=np.float32)
		else:
			scales = np.asarray(cfg.feature_scales, dtype=np.float32)
			if scales.shape != (W.shape[1], ):
				raise ValueError(f'feature scales shape {scales.shape} does not match {(W.shape[1], )}')
			self.scales = np.where(np.abs(scales) > 1e-8, scales, 1.0).astype(np.float32)

	@property
	def shape(self) -> Tuple[int, int]:
		return self.W.shape

	def value_and_grad_params(self, state_hex: str) -> Tuple[float, np.ndarray]:
		features = self.cli.features(state_hex)
		if features.shape != (self.W.shape[1], ):
			raise RuntimeError(f'features returned shape {features.shape}; weights expect {(self.W.shape[1], )}')
		features = features / self.scales
		if self.W.shape[0] == 1:
			alphas = np.ones((1, ), dtype=np.float32)
		else:
			phase = self.cfg.fixed_phase
			if phase is None:
				phase = self.cli.phase(state_hex)
			alphas = phase_alphas(float(phase), self.W.shape[0])

		effective_weights = (alphas[:, None] * self.W).sum(axis=0)
		raw = float(np.dot(effective_weights, features))
		tau = float(self.cfg.tau)
		if tau <= 0.0:
			raise ValueError('tau must be positive')
		value = math.tanh(raw / tau)
		dv_draw = (1.0 - value * value) / tau
		grad = dv_draw * (alphas[:, None] * features[None, :])
		return value, grad.astype(np.float32)

	def root_value_and_grad_from_leaf(self, leaf_state: str, leaf_depth: int) -> Tuple[float, np.ndarray]:
		leaf_value, leaf_grad = self.value_and_grad_params(leaf_state)
		sign = -1.0 if leaf_depth % 2 else 1.0
		return sign * leaf_value, (sign * leaf_grad).astype(np.float32)

@dataclass
class SearchConfig:
	depth: int = 4
	shuffle_moves: bool = True
	use_transposition_table: bool = True

@dataclass
class SearchResult:
	value: float
	best_move: Optional[str]
	leaf_state: str
	leaf_depth: int
	leaf_terminal: bool

@dataclass
class _TTEntry:
	value: float
	best_move: Optional[str]
	leaf_state: str
	leaf_depth: int
	leaf_terminal: bool

class AlphaBetaSearcher:
	def __init__(self, cli: CachedCLI, evaluator: LinearEvaluator, cfg: SearchConfig):
		self.cli = cli
		self.evaluator = evaluator
		self.cfg = cfg
																	#  Only exact nodes are cached.
																	#  Reusing alpha-beta bounds is valid for move selection, but their stored leaf
																	#  need not be the exact PV leaf required by TDLeaf's semi-gradient.
		self._tt: Dict[Tuple[str, int], _TTEntry] = {}

	def best_move_and_leaf(self, state_hex: str) -> SearchResult:
		self._tt.clear()											#  Weight updates make search values stale across TD steps.
		result = self._negamax(state_hex, self.cfg.depth, -float('inf'), float('inf'))
		if result.best_move is None:
			raise RuntimeError('No legal move returned for a non-terminal state')
		return result

	def _terminal_value(self, result: str) -> float:
		if result == 'win':
			return 1.0
		if result == 'loss':
			return -1.0
		if result == 'draw':
			return 0.0
		raise ValueError(f'Unknown terminal result: {result!r}')

	def _negamax(self, state_hex: str, depth: int, alpha: float, beta: float) -> SearchResult:
		key = (state_hex, depth)
		entry = self._tt.get(key) if self.cfg.use_transposition_table else None
		if entry is not None:
			return SearchResult(entry.value, entry.best_move, entry.leaf_state, entry.leaf_depth, entry.leaf_terminal)

		is_terminal, terminal_result = self.cli.terminal(state_hex)
		if is_terminal:
			value = self._terminal_value(str(terminal_result))
			result = SearchResult(value, None, state_hex, 0, True)
			if self.cfg.use_transposition_table:
				self._tt[key] = _TTEntry(value, None, state_hex, 0, True)
			return result

		if depth == 0:
			value, _ = self.evaluator.value_and_grad_params(state_hex)
			result = SearchResult(float(value), None, state_hex, 0, False)
			if self.cfg.use_transposition_table:
				self._tt[key] = _TTEntry(float(value), None, state_hex, 0, False)
			return result

		moves = self.cli.legal_moves(state_hex)
		if not moves:
																	#  Defensive fallback. terminal() should normally identify this state.
			return SearchResult(-1.0, None, state_hex, 0, True)
		if self.cfg.shuffle_moves:
			random.shuffle(moves)

		best = SearchResult(-float('inf'), None, state_hex, 0, False)
		cutoff = False
		for move in moves:
			child = self.cli.apply_move(state_hex, move)
			child_result = self._negamax(child, depth - 1, -beta, -alpha)
			value = -child_result.value
			if value > best.value:
				best = SearchResult(value=value, best_move=move, leaf_state=child_result.leaf_state, leaf_depth=child_result.leaf_depth + 1, leaf_terminal=child_result.leaf_terminal)
			alpha = max(alpha, value)
			if alpha >= beta:
				cutoff = True
				break

		if self.cfg.use_transposition_table and not cutoff:
			self._tt[key] = _TTEntry(best.value, best.best_move, best.leaf_state, best.leaf_depth, best.leaf_terminal)
		return best

class LinearTDLeaf:
	def __init__(self, evaluator: LinearEvaluator, *, learning_rate: float, lam: float, weight_decay: float=1e-5, trace_clip: float=10.0, grad_clip: float=10.0, weight_clip: float=1e6) -> None:
		self.evaluator = evaluator
		self.learning_rate = float(learning_rate)
		self.lam = float(lam)
		self.weight_decay = float(weight_decay)
		self.trace_clip = float(trace_clip)
		self.grad_clip = float(grad_clip)
		self.weight_clip = float(weight_clip)
		self.trace = np.zeros_like(evaluator.W, dtype=np.float32)

	def reset_traces(self) -> None:
		self.trace.fill(0.0)

	def update(self, delta: float, grad: np.ndarray) -> None:
		clipped_grad = np.clip(grad, -self.grad_clip, self.grad_clip)
																	#  Side-to-move POV alternates each ply, so old traces change sign.
		self.trace = -self.lam * self.trace + clipped_grad
		self.trace = np.clip(self.trace, -self.trace_clip, self.trace_clip)
		self.evaluator.W += (self.learning_rate * float(delta)) * self.trace
		if self.weight_decay:
			self.evaluator.W *= 1.0 - self.learning_rate * self.weight_decay
		self.evaluator.W = np.clip(self.evaluator.W, -self.weight_clip, self.weight_clip).astype(np.float32)

def searched_value_and_grad(evaluator: LinearEvaluator, search_result: SearchResult) -> Tuple[float, np.ndarray]:
	if search_result.leaf_terminal:
		return (float(search_result.value), np.zeros_like(evaluator.W, dtype=np.float32))
	_, grad = evaluator.root_value_and_grad_from_leaf(search_result.leaf_state, search_result.leaf_depth)
	return float(search_result.value), grad

def current_pov_target_from_successor(successor_value: float) -> float:
	return -float(successor_value)

def terminal_value(result: str) -> float:
	if result == 'win':
		return 1.0
	if result == 'loss':
		return -1.0
	if result == 'draw':
		return 0.0
	raise ValueError(f'Unknown terminal result: {result!r}')

def load_weights(path: str) -> Tuple[np.ndarray, int, Dict[str, Any]]:
	with open(path, 'r', encoding='utf-8') as handle:
		obj = json.load(handle)
	weights = np.asarray(obj['weights'], dtype=np.float32)
	if weights.ndim not in {1, 2}:
		raise ValueError(f'weights must be rank 1 or 2, got shape {weights.shape}')
	return weights, int(obj.get('step', 0)), dict(obj.get('extra', {}))

def save_weights(path: str, weights: np.ndarray, step: int, extra: Optional[Dict[str, Any]] = None) -> None:
	output = Path(path)
	output.parent.mkdir(parents=True, exist_ok=True)
	obj = {'weights': np.asarray(weights, dtype=np.float32).tolist(), \
	       'step': int(step),                                         \
	       'extra': extra or {}                                       }
	with output.open('w', encoding='utf-8') as handle:
		json.dump(obj, handle, indent=2)

def load_scales(path: str, feature_count: int) -> np.ndarray:
	with open(path, 'r', encoding='utf-8') as handle:
		obj = json.load(handle)
	scales = np.asarray(obj['feature_scales'], dtype=np.float32)
	if scales.shape != (feature_count, ):
		raise ValueError(f'feature_scales shape {scales.shape} does not match {(feature_count, )}')
	return scales

def checkpoint_name(base_path: str, step: int) -> str:
	path = Path(base_path)
	suffix = path.suffix or '.json'
	return str(path.with_name(f"{path.stem}-{step}{suffix}"))

def result_first_player_pov(label: str) -> float:
	normalized = label.strip().lower()
	if normalized in {'1-0', 'first', 'first-win', 'white', 'white-win', 'red', 'red-win'}:
		return 1.0
	if normalized in {'0-1', 'second', 'second-win', 'black', 'black-win'}:
		return -1.0
	if normalized in {'1/2-1/2', 'draw', 'd'}:
		return 0.0
	raise ValueError(f'Unknown game result: {label!r}')

def labeled_successor_value(result_label: str, next_first_player_to_move: bool) -> float:
	first_value = result_first_player_pov(result_label)
	return first_value if next_first_player_to_move else -first_value

def parse_indexed_move(token: str, profile: GameProfile) -> Tuple[int, int, Optional[str]]:
	parts = token.strip().replace('x', '-').replace('X', '-').replace(':', '-').split('-')
	parts = [part for part in parts if part != '']
	if len(parts) not in {2, 3}:
		raise ValueError(f'Bad transcript move: {token!r}')
	src = int(parts[0])
	dst = int(parts[1])
	if not (0 <= src < profile.board_squares and 0 <= dst < profile.board_squares):
		raise ValueError(f'Square out of range in {token!r}')
	promo: Optional[str] = None
	if len(parts) == 3:
		promo = parts[2].strip().upper()
		if promo not in profile.promotion_codes:
			raise ValueError(f'Bad promotion in {token!r}')
	return src, dst, promo

def encode_indexed_move(token: str, profile: GameProfile) -> str:
	src, dst, promo = parse_indexed_move(token, profile)
	promo_code = 0 if promo is None else profile.promotion_codes[promo]
	return bytes([src, dst, promo_code]).hex()

def normalized_printed_move(payload: Dict[str, Any]) -> Tuple[int, int, Optional[str]]:
	if 'move_from' in payload and 'move_to' in payload:
		promo = payload.get('promo')
		return int(payload['move_from']), int(payload['move_to']), None if promo is None else str(promo).upper()
	if 'from' in payload and 'to' in payload:
		promo = payload.get('promo')
		return int(payload['from']), int(payload['to']), None if promo is None else str(promo).upper()
	seq = payload.get('move_seq')
	if isinstance(seq, list) and len(seq) == 2:
		return int(seq[0]), int(seq[1]), None
	raise RuntimeError(f'Unsupported print_move response: {payload!r}')

def find_transcript_move(cli: CachedCLI, state_hex: str, token: str, profile: GameProfile, *, direct_three_byte_encoding: bool=True) -> str:
	legal = cli.legal_moves(state_hex)
	if direct_three_byte_encoding:
		candidate = encode_indexed_move(token, profile)
		if candidate in legal:
			return candidate

	target = parse_indexed_move(token, profile)
	matches: List[str] = []
	for move_hex in legal:
		if normalized_printed_move(cli.print_move(move_hex)) == target:
			matches.append(move_hex)
	if len(matches) == 1:
		return matches[0]
	if not matches:
		raise ValueError(f'No legal move matches transcript token {token!r}')
	raise ValueError(f'Ambiguous transcript move {token!r}: {matches}')

def print_board_payload(payload: Dict[str, Any]) -> None:
	row_keys = [key for key in payload if key.startswith('row_')]
	def row_number(key: str) -> int:
		try:
			return int(key.split('_', 1)[1])
		except ValueError:
			return -1
	for key in sorted(row_keys, key=row_number, reverse=True):
		print(' '.join(list(str(payload[key]).replace('x', ' '))))
	for side_key in ('white_to_move', 'black_to_move', 'red_to_move', 'first_to_move'):
		if side_key in payload:
			print(f'{side_key}={bool(payload[side_key])}')
			break
