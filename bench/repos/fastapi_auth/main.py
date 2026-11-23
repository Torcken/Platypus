from fastapi import FastAPI
from pydantic import BaseModel
from typing import Optional
import datetime

app = FastAPI()

class User(BaseModel):
    username: str
    email: str

@app.post("/login")
async def login(user: User):
    # TODO: Implement JWT generation
    return {"message": "Login successful", "token": "dummy_token"}

@app.get("/health")
async def health():
    return {"status": "ok"}
