'use strict';

const hsl = (h, s, l) => `hsl(${h}, ${s}%, ${l}%)`
const ind = (x, y) => y * 8 + x
const between = (x, a, b) => a <= x && x < b

const Pieces = {
    king:   0,
    queen:  1,
    bishop: 2,
    knight: 3,
    rook:   4,
    pawn:   5
}

const Side = {
    black: 0,
    white: 1
}

const GameStatus = {
    draw: -2,
    stalemate: -1,
    normal: 0,
    check: 1,
    checkmate: 2
}

var playerSide = Side.white
var boardOrientation = playerSide
var currentSide = Side.white

const flip = x => boardOrientation == Side.black ? 7 ^ x : x

class ChessPiece {
    constructor(type, side, x, y) {
        this.type = type
        this.side = side
        this.x = x
        this.y = y
    }
    
    updateUI() {
        if (this.element) {
            this.element.style.backgroundPosition = `${this.type * 20}% ${(1 - this.side) * 100}%`
            this.element.style.left = flip(this.x) * 12.5 + '%'
            this.element.style.top  = flip(this.y) * 12.5 + '%'
        }
        
        return this
    }
    
    hide() {
        if (this.element)
            this.element.style.opacity = 0
        
        return this
    }
    
    clone() {
        return new ChessPiece(this.type, this.side, this.x, this.y)
    }
}

class ChessBoard {
    constructor() {
        this.pieces = new Map()
        this.material = [[0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0]]
        this.moves = []
    }
    
    rebuildStatistics() {
        this.material = [[0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0]]
        
        for (const piece of board.pieces.values())
            this.material[piece.side][piece.type]++
    }
    
    addPiece(type, side, x, y) {
        this.material[side][type]++
        this.pieces.set(ind(x, y), new ChessPiece(type, side, x, y))
    }
    
    makeMove(piece, x, y) {
        this.pieces.delete(ind(piece.x, piece.y))
        var dx = x - piece.x
        var dy = y - piece.y
        var changeX = Math.abs(dx)
        var changeY = Math.abs(dy)
   
        var index = ind(x, y)

        // This is castling, move the rook
        if (piece.type == Pieces.king && changeX == 2) {
            var rook_ind = ind(dx < 0 ? 0 : 7, piece.y)
            var rook_piece = this.pieces.get(rook_ind)
            this.pieces.delete(rook_ind)
            var rook_nx = rook_piece.x = x - dx/2
            var rook_ny = rook_piece.y = piece.y
            this.pieces.set(ind(rook_nx, rook_ny), rook_piece.updateUI())
        }
                
        var captured = this.pieces.get(index)
        
        // This is en passant, capture the enemy pawn a rank behind
        if (!captured && piece.type == Pieces.pawn && changeX == 1)
            captured = this.pieces.get(index = ind(x, piece.side == Side.white ? y+1 : y-1))
        
        if (captured) {
            this.material[captured.side][captured.type]--
            captured.hide()
        }
        
        this.pieces.delete(index)
                
        var thisMove = {
            movingPiece: piece,
            oldX: piece.x, oldY: piece.y,
            newX: x, newY: y
        }

        piece.lastMove = {oldX: piece.x, oldY: piece.y, newX: x, newY: y}
        piece.x = x
        piece.y = y
        
        // Promote to queen
        if (piece.type == Pieces.pawn &&
            piece.y == (1 - piece.side) * 7) {
            piece.type = Pieces.queen
            this.material[piece.side][Pieces.pawn]--
            this.material[piece.side][piece.type]++
        }
        
        this.pieces.set(ind(x, y), piece.updateUI())
        this.moves.push(thisMove)
    }
    
    findByElement(element) {
        for (const e of this.pieces.values())
            if (e.element == element)
                return e
        return null
    }
    
    find(func) {
        for (const e of this.pieces.values())
            if (func(e))
                return e
    }
    
    branchOut(piece, x, y) {
        var b = new ChessBoard()
        var counterpart = null
        
        this.pieces.forEach(e => {
            let co = e.clone()
            if (e.x == piece.x && e.y == piece.y)
                counterpart = co
            b.pieces.set(ind(e.x, e.y), co)
        })
        
        b.material = [[], []]
        b.totalMoves = this.totalMoves + 1
        
        for (var i = 0; i < 2; i++)
            for (var j = 0; j < 6; j++)
                b.material[i].push(this.material[i][j])
                
        b.makeMove(counterpart, x, y)
        return b
    }
    
    inCheck(side) {
        var king = this.find(p => p.type == Pieces.king && p.side == side)
        
        var check = false
                
        for (const piece of this.pieces.values())
            check |= piece.side != side &&
                !!this.legalMoves(piece, false).some(
                    m => m[0] == king.x && m[1] == king.y)
                
        return check
    }
        
    allLegalMoves() {
        let moves = [[], []]

        for (const piece of this.pieces.values())
            this.legalMoves(piece, true).forEach(x => moves[piece.side].push([piece, x[0], x[1]]))
            
        return moves
    }
    
    status() {
        var s = [GameStatus.normal, GameStatus.normal]
        var checks = [this.inCheck(Side.black), this.inCheck(Side.white)]
        var moveCounts = [0, 0]
        
        var pieceCount = 0
        
        for (const piece of this.pieces.values()) {
            pieceCount++
            moveCounts[piece.side] += this.legalMoves(piece, true).length
        }
        
        // In standard chess, we can assume that any remaining two pieces are both kings
        // So, it's a draw
        if (pieceCount == 2)
            return [GameStatus.draw, GameStatus.draw]
        
        for (var i = 0; i < 2; i++)
            s[i] = checks[i]
                ? moveCounts[i] ? GameStatus.check : GameStatus.checkmate
                : moveCounts[i] ? GameStatus.normal : GameStatus.stalemate
                
        return s
    }
    
    start() {
        Object.values(Side).forEach(side => {
            const list = [Pieces.rook, Pieces.knight, Pieces.bishop]

            for (var i = 0; i <= 1; i++)
            for (var j = 0; j < 3; j++)
                this.addPiece(list[j], side, i ? 7 - j : j, side * 7)
            
            for (var i = 0; i < 8; i++)
                this.addPiece(Pieces.pawn, side, i, side ? 6 : 1)
            
            this.addPiece(Pieces.queen, side, 3, side * 7)
            this.addPiece(Pieces.king, side, 4, side * 7)
        })
    }
    
    isOccupied(piece, ox, oy) {
        return this.pieces.has(ind(piece.x + ox, piece.y + oy))
    }
    
    legalMoves(piece, checkForCheck) {
        let moves = []
        let side = piece.side
        let fside = side ? -1 : 1
        
        let tryAdd = (ox, oy, allowEnemy, allowFriend, needCapture) => {
            let nx = piece.x + ox, ny = piece.y + oy
            
            if (nx >= 8 || ny >= 8 || nx < 0 || ny < 0)
                return false
            
            let found = this.pieces.get(ind(nx, ny))
            
            if (needCapture && !found
                || found &&
                (found.side == side ? !allowFriend : !allowEnemy))
                return false
                
            let branch = this.branchOut(piece, nx, ny)
            
            if (checkForCheck && branch.inCheck(side))
                return false
                
           moves.push([nx, ny])
           return true
        }
        
        let queenMoves = (rook, bishop) => {
            if (rook) {
                for (var i = -1; i <= 1; i += 2)
                for (var j = 0; j <= 1; j++) {
                    var x = 0, y = 0
                    
                    do { 
                        if (j) x += i
                        else y += i
                        tryAdd(x, y, true, false, false)
                    } while (between(piece.x + x, 0, 8) &&
                        between(piece.y + y, 0, 8) &&
                        !this.isOccupied(piece, x, y))
                }
            }
            
            if (bishop) {
                for (var i = -1; i <= 1; i += 2)
                for (var j = -1; j <= 1; j += 2) {
                    var x = 0, y = 0
                    
                    do tryAdd(x += i, y += j, true, false, false)
                    while (between(piece.x + x, 0, 8) &&
                        between(piece.y + y, 0, 8) &&
                        !this.isOccupied(piece, x, y))
                }
            }
        }
        
        switch (piece.type) {
        case Pieces.pawn:
            if (piece.y == (piece.side ? 6 : 1) && !this.isOccupied(piece, 0, fside))
                tryAdd(0, fside * 2, false, false, false)
            
            // En passant
            if (this.moves.length) {
                let enemy = this.moves[this.moves.length - 1].movingPiece
                
                let dx = enemy.lastMove.newX - piece.x
    
                if (enemy.side != piece.side
                    && enemy.type == Pieces.pawn
                    && Math.abs(enemy.lastMove.newY - enemy.lastMove.oldY) == 2
                    && Math.abs(dx) == 1
                    && enemy.lastMove.newY == piece.y)
                    tryAdd(dx, fside, true, false, false)
            }
            
            tryAdd(0, fside,  false, false, false)
            tryAdd(1, fside,  true, false, true)
            tryAdd(-1, fside, true, false, true)
            
            break
        
        case Pieces.knight:
            for (var i = -2; i <= 2; i += 4) {
                for (var j = -1; j <= 1; j += 2) {
                    tryAdd(i, j, true, false, false)
                    tryAdd(j, i, true, false, false)
                }
            }
            
            break
            
        case Pieces.king:
            let lrook = this.pieces.get(ind(0, piece.y))
            let rrook = this.pieces.get(ind(7, piece.y))
            
            let canCastle = rook => {
                if (rook.type != Pieces.rook || rook.side != side)
                    return false
                
                let dx = Math.sign(rook.x - piece.x)
                
                var allowed = !rook.lastMove && !piece.lastMove
                
                var X = -1
                
                for (var i = 1; allowed && between(X = piece.x + i * dx, 2, 7); i++)
                    allowed &= !this.isOccupied(piece, i * dx, 0)
                        && (i == 1 || !this.branchOut(piece, X, piece.y).inCheck(piece.side))
                
                return allowed
            }
            
            // Determine castling possibilities
            if (checkForCheck && !this.inCheck(piece.side)) {
                if (lrook && canCastle(lrook)) tryAdd(-2, 0, false, false, false)
                if (rrook && canCastle(rrook)) tryAdd(+2, 0, false, false, false)
            }
            
            for (var j = -1; j <= 1; j += 2) {
                for (var i = -1; i <= 1; i++)
                    tryAdd(i, j, true, false, false)
                tryAdd(j, 0, true, false, false)
            }
            break
            
        case Pieces.queen:
            queenMoves(true, true)
            break
            
        case Pieces.rook:
            queenMoves(true, false)
            break
            
        case Pieces.bishop:
            queenMoves(false, true)
            break
        }
        
        return moves
    }
}

var moveCounter = 0
var gameOver = false
var boardSquares = []

var curMovePiece = null
var curMovePointers = []

var highlightedSquares = []

var board = new ChessBoard()

const switchSide = () => {
    currentSide ^= 1
}

const highlightTheme = [
    [hsl(150, 40, 50), 0, 5, 2],
    [hsl(170, 60, 50), 0, 5, 2],
    [hsl(200, 50, 60), 30, 5, 2],
    [hsl(0, 80, 60), 20, 40, 10]
]

const highlightSquare = (x, y, i) => {
    var element = boardSquares[ind(flip(x), flip(y))]
    
    if (element) {
        let entry = highlightTheme[i]
        
        element.style.setProperty('--color', entry[0])
        element.style.boxShadow = `0px 0px ${entry[2]}px ${entry[3]}px var(--color)`
        element.style.borderRadius = `${entry[1]}%`
        element.style.zIndex = 2
            
        highlightedSquares.push([element, i])
    }
}

let squareBaseHue = 200
let colorSquare = (x, y) => hsl(squareBaseHue + y * 10, 100, x+y & 1 ? 30 : 70)

const _unhighlight = e => {
    let coords = e.getAttribute('boardCoords')
    e.style.setProperty('--color', colorSquare(parseInt(coords[0]), parseInt(coords[2])))
    e.style.boxShadow = ''
    e.style.borderRadius = '4%'
    e.style.zIndex = 1
}

const unhighlightSquare = (x, y) => {
    const index = ind(flip(x), flip(y))
    var element = boardSquares[index]
    
    if (element) {
        _unhighlight(element)
        highlightedSquares.splice(index, 1)
    }
}

const unhighlightSquares = i => {
    highlightedSquares = highlightedSquares.filter(s => {
        var match = s[1] != i
        
        if (!match)
            _unhighlight(s[0])
        
        return match
    })
}

const postMove = () => {
    if (gameOver)
        return
        
    unhighlightSquares(3)
        
    var s = board.status()
    
    for (var i = Side.black; i <= Side.white; i++) {
        if (s[i] >= GameStatus.check) {
            var king = board.find(x => x.type == Pieces.king && x.side == i)
            
            if (king)
                highlightSquare(king.x, king.y, 3)
        }
    }

    gameOver = true
    
    if (s[0] == GameStatus.checkmate)
        alert('Checkmate for black')
    else if (s[1] == GameStatus.checkmate)
        alert('Checkmate for white')
    else if (s[0] == GameStatus.stalemate || s[1] == GameStatus.stalemate)
        alert('Stalemate')
    else if (s[0] == GameStatus.draw)
        alert('Draw')
    else
        gameOver = false
    
    moveCounter++
}

var initialBoardString = ''

String.prototype.replaceAt = function(index, replacement) {
    return this.substring(0, index) + replacement + this.substring(index + replacement.length);
}

class AIController {
    constructor(board, side) {
        this.board = board
        this.side = side
        this.calls = 0
    }
    
    
    evaluate(board, side) {
        var materialWeights = [20, 9, 3, 3, 5, 1]
        var mobilityWeight = .1

        var value = 0
        
        for (var i = Pieces.king; i <= Pieces.pawn; i++)
            value += materialWeights[i] * (board.material[1][i] - board.material[0][i])

        return value * (2 * side - 1)
    }
    
    search(board, maxDepth, alpha, beta, side) {
        var value = -Infinity
                            
        var bestMove = null
        
        if (maxDepth == 0)
            return [this.evaluate(board, side), null]
        
        var moves = board.allLegalMoves()[side].map(x =>
            [x, this.evaluate(board.branchOut(x[0], x[1], x[2]), side)])
            .sort((a, b) => b[1] - a[1])
            
        for (const child of moves) {
            var m = -this.search(
                board.branchOut(child[0][0], child[0][1], child[0][2]),
                maxDepth - 1, -beta, -alpha, side ^ 1)[0]
                            
            if (m > value) {
                value = m
                bestMove = child[0]
            }
            
            alpha = Math.max(alpha, value)
            
            if (alpha >= beta)
                break
        }
        
        return [value, bestMove]
    }
    
    async move() {
        setTimeout(() => {
            let finishMove = result => {
                var move = result[1]
                highlightSquare(move[0].x, move[0].y, 0)
                highlightSquare(move[1], move[2], 1)

                board.makeMove(move[0], move[1], move[2])
                    
                switchSide()
                postMove()
            }
            
            const xhttp = new XMLHttpRequest()
            const board = this.board
                
            xhttp.onload = function() {
                var response = this.responseText.split(' ')
                    
                console.log(response)
                let piece = board.pieces.get(ind(+response[0], +response[1]))
                    
                finishMove([0, [
                        piece,
                        +response[2],
                        +response[3]]])
            }
                     
            var max_depth = +document.getElementById('max_depth').value
            var max_time = +document.getElementById('max_time').value.replace(',', '.')
                     
            xhttp.open('POST', `chess_engine`, true)
            xhttp.send(
                `${initialBoardString}\n${max_depth} ${max_time} ${this.board.moves.length}\n` +
                this.board.moves.map(m => `${m.oldX} ${m.oldY} ${m.newX} ${m.newY}`).join('\n') +
                '\n')
        }, 500)
    }
}

var downEventType = null
var ai = new AIController(board, playerSide ^ 1)

window.addEventListener('load', () => {
    var divBoard = document.createElement('div')
    divBoard.className = 'board'
        
    divBoard.style.backgroundColor = hsl(squareBaseHue, 100, 30)
                
    for (var y = 0; y < 8; y++) {
        for (var x = 0; x < 8; x++) {
            var square = document.createElement('div')
            square.setAttribute('boardCoords', x + ',' + y)
            square.className = 'board_square'
            square.style.setProperty('--color', colorSquare(x, y))
            divBoard.appendChild(square)
            boardSquares.push(square)
        }
                
        divBoard.appendChild(document.createElement('br'))
    }
    
    board.start()
    
    initialBoardString = '0'.repeat(64)
                
    for (const piece of board.pieces.values())
        initialBoardString = initialBoardString.replaceAt(piece.x + piece.y * 8, (piece.type + 1 | piece.side << 3).toString('16'))
    
    let mouseDown = event => {
        event.stopPropagation()
        
        // Register only a single event type to prevent duplicates
        if (!downEventType && event.type.match(/touchstart|mousedown/g))
            downEventType = event.type
                        
        if (!gameOver && currentSide == playerSide) {            
            let boardRect = divBoard.getBoundingClientRect()
            let X = (event.clientX || event.touches[0].pageX) - boardRect.left
            let Y = (event.clientY || event.touches[0].pageY) - boardRect.top
            let xfactor = 8 / boardRect.width
            let yfactor = 8 / boardRect.height
            
            var coords = [flip(Math.floor(X * xfactor)), flip(Math.floor(Y * yfactor))]
            
            let piece = board.pieces.get(ind(coords[0], coords[1]))

            if (!curMovePiece && event.type == downEventType) {
                if (piece && piece.side == playerSide) {
                    highlightSquare(coords[0], coords[1], 0)
                    curMovePiece = piece

                    board.legalMoves(piece, true).forEach(x => {
                        var ptr = document.createElement('div')
                        x[1]
                        
                        ptr.className = 'board_move_pointer'
                        ptr.style.left = `${(flip(x[0]) + .5) * 12.5 - 4/2}%`
                        ptr.style.top = `${(flip(x[1]) + .5) * 12.5 - 4/2}%`
                        
                        var captured = board.pieces.get(ind(x[0], x[1]))
                        
                        if (captured)
                            highlightSquare(captured.x, captured.y, 2)
                        
                        curMovePointers.push([x, ptr])
                        divBoard.appendChild(ptr)
                    })
                }
            }
            else {
                if (event.type == downEventType && piece && piece.side == playerSide) {
                    var same = piece.x == curMovePiece.x && piece.y == curMovePiece.y
                    
                    unhighlightSquare(curMovePiece.x, curMovePiece.y)
                    unhighlightSquares(2)

                    curMovePiece = null
                    curMovePointers.forEach(e => divBoard.removeChild(e[1]))
                    curMovePointers.length = 0

                    if (!same)
                        mouseDown(event)
                }
                
                if (curMovePointers.some(e => e[0][0] == coords[0] && e[0][1] == coords[1])) {
                    for (var i = 0; i < 3; i++)
                        unhighlightSquares(i)

                    highlightSquare(curMovePiece.x, curMovePiece.y, 0)
                    highlightSquare(coords[0], coords[1], 1)
                    
                    board.makeMove(curMovePiece, coords[0], coords[1])
                    switchSide()
                    postMove()

                    ai.move()
                    
                    curMovePiece = null
                    curMovePointers.forEach(e => divBoard.removeChild(e[1]))
                    curMovePointers.length = 0
                }
            }
        }
    }
    
    let mouseUp = event => {
        event.stopPropagation()

        if (curMovePiece)
            mouseDown(event)
    }
        
    if (currentSide == Side.black)
        ai.move()

    divBoard.addEventListener('mousedown', mouseDown)
    divBoard.addEventListener('mouseup', mouseUp)
    divBoard.addEventListener('touchstart', mouseDown)
    divBoard.addEventListener('touchend', mouseUp)

    board.pieces.forEach(piece => {
        var square = document.createElement('div')
        square.className = 'board_piece'
        piece.element = square
        square.style.backgroundImage = 'url(pieces.png)'
        piece.updateUI()
        divBoard.appendChild(square)
    })
        
    document.body.appendChild(divBoard)
    
});