
document.querySelectorAll('#tb .tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('#tb .tab').forEach(x=>x.classList.toggle('on',x===b));im.src='assets/'+b.dataset.k+'.png'});
