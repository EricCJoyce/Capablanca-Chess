#!/usr/bin/env python3
'''
Self-play TDLeaf(lambda) training for linear side-to-move feature evaluators.

Completed self-play games are appended to a reconstructible JSONL transcript.
For games with randomized starting arrangements, the exact start state is saved.
'''

from __future__ import annotations

import argparse
import random
from dataclasses import dataclass

import numpy as np

from linear_tdleaf_common import (AlphaBetaSearcher, CachedCLI, EvalConfig, GAME_PROFILES,
                                  GameTranscriptWriter, LinearEvaluator, LinearTDLeaf, SearchConfig,
                                  checkpoint_name, current_pov_target_from_successor,
                                  default_game_transcript_path, load_scales, load_weights,
                                  print_board_payload, save_weights, searched_value_and_grad,
                                  terminal_value)

@dataclass
class TrainConfig:
	episodes: int
	max_plies: int
	epsilon: float
	checkpoint_every: int
	verbose: bool

def train(cli: CachedCLI, evaluator: LinearEvaluator, learner: LinearTDLeaf, searcher: AlphaBetaSearcher, cfg: TrainConfig, weights_out: str, starting_episode: int, game_name: str, transcript_writer: GameTranscriptWriter) -> None:
	for episode in range(starting_episode + 1, starting_episode + cfg.episodes + 1):
		state = cli.startpos()
		start_state = state
		moves = []
		exploration_plies = []
		learner.reset_traces()
		outcome = 'cap-draw'
		plies = 0
		abs_delta_sum = 0.0
		updates = 0

		while plies < cfg.max_plies:
			is_terminal, result = cli.terminal(state)
			if is_terminal:
				outcome = str(result)
				break

			current_search = searcher.best_move_and_leaf(state)
			value_t, grad_t = searched_value_and_grad(evaluator, current_search)

			legal = cli.legal_moves(state)
			if not legal:
				outcome = 'no-legal-moves'
				break
																	#  Preserve the trainer's original RNG call order:
																	#  one epsilon draw, followed by random.choice only when exploration is selected.
			explored = random.random() < cfg.epsilon
			move = random.choice(legal) if explored else str(current_search.best_move)
			next_state = cli.apply_move(state, move)
			plies += 1
			moves.append(move)
			if explored:
				exploration_plies.append(plies)

			if cfg.verbose:
				print()
				print_board_payload(cli.draw(next_state))

			next_terminal, next_result = cli.terminal(next_state)
			if next_terminal:
				successor_value = terminal_value(str(next_result))
				outcome = str(next_result)
			elif plies >= cfg.max_plies:
				successor_value = 0.0
				outcome = 'cap-draw'
			else:
				next_search = searcher.best_move_and_leaf(next_state)
				successor_value, _ = searched_value_and_grad(evaluator, next_search)

			target = current_pov_target_from_successor(successor_value)
			delta = target - value_t
			learner.update(delta, grad_t)
			abs_delta_sum += abs(delta)
			updates += 1

			if cfg.verbose:
				move_readable = cli.print_move(move)
				print(f'ep={episode} ply={plies} move={move_readable} v={value_t:+.5f} target={target:+.5f} delta={delta:+.5f}')

			state = next_state
			if next_terminal:
				break

		mean_abs_delta = abs_delta_sum / max(1, updates)
		stats = cli.stats()
		extra = {'kind': 'self-play-linear-tdleaf',       \
		         'game': game_name,                       \
		         'plies': plies,                          \
		         'outcome': outcome,                      \
		         'mean_abs_delta': mean_abs_delta,        \
		         'weight_shape': list(evaluator.W.shape), \
		         'cli': stats                             }
																	#  start_state + moves is sufficient to reconstruct every visited state.
																	#  final_state is included as a convenient integrity check during replay.
		transcript_writer.write_game({'kind': 'self-play-linear-tdleaf',       \
		                              'game': game_name,                       \
		                              'episode': episode,                      \
		                              'start_state': start_state,              \
		                              'moves': moves,                          \
		                              'final_state': state,                    \
		                              'plies': plies,                          \
		                              'outcome': outcome,                      \
		                              'mean_abs_delta': mean_abs_delta,        \
		                              'max_plies': cfg.max_plies,              \
		                              'epsilon': cfg.epsilon,                  \
		                              'search_depth': searcher.cfg.depth,      \
		                              'exploration_plies': exploration_plies,  \
		                              'weight_shape': list(evaluator.W.shape), \
		                              'tau': evaluator.cfg.tau,                \
		                              'fixed_phase': evaluator.cfg.fixed_phase })

		if episode % cfg.checkpoint_every == 0:
			save_weights(checkpoint_name(weights_out, episode), evaluator.W, episode, extra)

		norm = float(np.linalg.norm(evaluator.W))
		print(f'episode={episode:6d} plies={plies:4d} outcome={outcome:12s} |delta|={mean_abs_delta:.6f} ||W||={norm:.4f} calls={stats["cli_calls"]} hits={stats["cache_hits"]} starts={stats["process_starts"]}')

	final_episode = starting_episode + cfg.episodes
	save_weights(weights_out, evaluator.W, final_episode, {'kind': 'self-play-linear-tdleaf',       \
	                                                       'game': game_name,                       \
	                                                       'weight_shape': list(evaluator.W.shape), \
	                                                       'cli': cli.stats()                       })

def build_argument_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser()
	parser.add_argument('--game', required=True, choices=sorted(GAME_PROFILES))
	parser.add_argument('--cli', required=True)
	parser.add_argument('--weights-in', required=True)
	parser.add_argument('--weights-out', required=True)
	parser.add_argument('--transcript-out', default='', help='Append completed games as JSONL; default is <weights-stem>-games.jsonl')
	parser.add_argument('--no-transcript', action='store_true', help='Disable game transcript logging')
	parser.add_argument('--scales', default='')
	parser.add_argument('--depth', type=int, default=4)
	parser.add_argument('--episodes', type=int, default=200)
	parser.add_argument('--max-plies', type=int, default=0, help='0 uses the game profile default')
	parser.add_argument('--lr', type=float, default=1e-3)
	parser.add_argument('--lam', type=float, default=0.7)
	parser.add_argument('--tau', type=float, default=3.0)
	parser.add_argument('--epsilon', type=float, default=0.05)
	parser.add_argument('--weight-decay', type=float, default=1e-5)
	parser.add_argument('--trace-clip', type=float, default=10.0)
	parser.add_argument('--grad-clip', type=float, default=10.0)
	parser.add_argument('--weight-clip', type=float, default=1e6)
	parser.add_argument('--checkpoint-every', type=int, default=1)
	parser.add_argument('--seed', type=int, default=1)
	parser.add_argument('--fixed-phase', type=float, default=None)
	parser.add_argument('--cache-entries', type=int, default=250_000)
	parser.add_argument('--one-shot-cli', action='store_true', help='Disable persistent JSON-lines mode')
	parser.add_argument('--no-shuffle', action='store_true')
	parser.add_argument('--no-tt', action='store_true')
	parser.add_argument('--verbose', action='store_true')
	return parser

def main() -> None:
	args = build_argument_parser().parse_args()

	random.seed(args.seed)
	np.random.seed(args.seed)
	profile = GAME_PROFILES[args.game]
	weights, starting_episode, checkpoint_extra = load_weights(args.weights_in)
	feature_count = weights.shape[-1]
	scales = load_scales(args.scales, feature_count) if args.scales else None
	max_plies = args.max_plies if args.max_plies > 0 else profile.default_max_plies

	print(f'resuming step={starting_episode} from={args.weights_in} weight_shape={tuple(weights.shape)}')
	if checkpoint_extra:
		print(f'checkpoint_extra={checkpoint_extra}')

	transcript_out = '' if args.no_transcript else (args.transcript_out or default_game_transcript_path(args.weights_out))
	if transcript_out:
		print(f'transcript_out={transcript_out}')

	with CachedCLI(args.cli, prefer_persistent=not args.one_shot_cli, cache_entries=args.cache_entries) as cli, GameTranscriptWriter(transcript_out) as transcript_writer:
		evaluator = LinearEvaluator(cli, weights, EvalConfig(tau=args.tau, feature_scales=scales, fixed_phase=args.fixed_phase))
		learner = LinearTDLeaf(evaluator, learning_rate=args.lr, lam=args.lam, weight_decay=args.weight_decay,   \
		                       trace_clip=args.trace_clip, grad_clip=args.grad_clip, weight_clip=args.weight_clip)
		searcher = AlphaBetaSearcher(cli, evaluator, SearchConfig(depth=args.depth, shuffle_moves=not args.no_shuffle, use_transposition_table=not args.no_tt))
		train(cli, evaluator, learner, searcher, TrainConfig(episodes=args.episodes,
		                                                     max_plies=max_plies,
		                                                     epsilon=args.epsilon,
		                                                     checkpoint_every=args.checkpoint_every,
		                                                     verbose=args.verbose),
		      args.weights_out, starting_episode, profile.name, transcript_writer)
	return

if __name__ == '__main__':
	main()
