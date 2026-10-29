"""Pong in PRG32 Python: the same game as c/game.c, one statement at a time."""
import prg32

PADDLE_W: int = 64
PADDLE_H: int = 8
BALL: int = 8

paddle_x: int = 128
paddle2_x: int = 128
ball_x: int = 156
ball_y: int = 96
ball_dx: int = 2
ball_dy: int = 1


def init() -> None:
    """Runs once, when the cartridge starts: put everything in its place."""
    global paddle_x, paddle2_x, ball_x, ball_y, ball_dx, ball_dy
    paddle_x = 128
    paddle2_x = 128
    ball_x = 156
    ball_y = 96
    ball_dx = 2
    ball_dy = 1


def update() -> None:
    """Runs every frame: move the paddles and the ball, then bounce."""
    global paddle_x, paddle2_x, ball_x, ball_y, ball_dx, ball_dy
    if prg32.held(prg32.LEFT):
        paddle_x -= 3
    if prg32.held(prg32.RIGHT):
        paddle_x += 3
    paddle_x = prg32.clamp(paddle_x, 0, prg32.SCREEN_W - PADDLE_W)

    # The upper paddle follows the ball: a very simple computer player.
    if ball_x + BALL // 2 < paddle2_x + PADDLE_W // 2:
        paddle2_x -= 1
    elif ball_x + BALL // 2 > paddle2_x + PADDLE_W // 2:
        paddle2_x += 1
    paddle2_x = prg32.clamp(paddle2_x, 0, prg32.SCREEN_W - PADDLE_W)

    ball_x += ball_dx
    ball_y += ball_dy
    if ball_x < 0 or ball_x > prg32.SCREEN_W - BALL:
        ball_dx = -ball_dx
    if prg32.touching(ball_x, ball_y, BALL, BALL, paddle2_x, 4, PADDLE_W, PADDLE_H):
        ball_dy = 1
    if prg32.touching(ball_x, ball_y, BALL, BALL, paddle_x, 188, PADDLE_W, PADDLE_H):
        ball_dy = -1
    if ball_y < 0 or ball_y > prg32.SCREEN_H - BALL:
        ball_dy = -ball_dy


def draw() -> None:
    """Runs every frame after update: paint the state that update computed."""
    prg32.clear(prg32.BLACK)
    prg32.rect(paddle2_x, 4, PADDLE_W, PADDLE_H, prg32.CYAN)
    prg32.rect(paddle_x, 188, PADDLE_W, PADDLE_H, prg32.WHITE)
    prg32.rect(ball_x, ball_y, BALL, BALL, prg32.YELLOW)
    prg32.text(8, 96, "PONG PYTHON", prg32.GREEN, prg32.BLACK)
